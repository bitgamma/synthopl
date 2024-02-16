#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_ota_ops.h"
#include "freertos/FreeRTOSConfig.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"
#include "gatt_svr.h"
#include "opl_srv.h"
#include "synth.h"
#include "esp_log.h"

#define REBOOT_DEEP_SLEEP_TIMEOUT 500

static const char *manuf_name = "Bitgamma";
static const char *model_num = "Synth OPL";
static const char *TAG = "gatt_srv";

static uint16_t conn_handle;
static uint8_t ble_synth_prph_addr_type;

static uint8_t gatt_svr_chr_ota_control_val;
static uint8_t gatt_svr_chr_ota_data_val[512];

static uint16_t ota_control_val_handle;
static uint16_t ota_data_val_handle;

static const esp_partition_t *update_partition;
static esp_ota_handle_t update_handle;
static bool updating = false;
static uint16_t num_pkgs_received = 0;
static uint16_t packet_size = 0;

static int ble_synth_prph_gap_event(struct ble_gap_event *event, void *arg);

static int gatt_svr_chr_opl_msg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_opl_list_prg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_opl_program(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static int gatt_svr_chr_ota_control_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_ota_data_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
  {
    /* Service: SynthOPL */
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID128_DECLARE(GATT_OPL_UUID),
    .characteristics = (struct ble_gatt_chr_def[]) { 
      {
        /* Characteristic: OPL Message */
        .uuid = BLE_UUID128_DECLARE(GATT_OPL_CHR_UUID_MSG),
        .access_cb = gatt_svr_chr_opl_msg,
        .flags = BLE_GATT_CHR_F_WRITE,
      }, {
        /* Characteristic: List programs */
        .uuid = BLE_UUID128_DECLARE(GATT_OPL_CHR_UUID_LIST_PRG),
        .access_cb = gatt_svr_chr_opl_list_prg,
        .flags = BLE_GATT_CHR_F_READ,
      }, {
        /* Characteristic: Program */
        .uuid = BLE_UUID128_DECLARE(GATT_OPL_CHR_UUID_PROGRAM),
        .access_cb = gatt_svr_chr_opl_program,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
      }, {
        0, /* No more characteristics in this service */
      },
    }
  }, 

  {
    /* Service: OTA Update */
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID128_DECLARE(GATT_OTA_UUID),
    .characteristics = (struct ble_gatt_chr_def[]) { 
      {
        /* Characteristic: OPL Message */
        .uuid = BLE_UUID128_DECLARE(GATT_OTA_CHR_CTRL),
        .access_cb = gatt_svr_chr_ota_control_cb,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &ota_control_val_handle,
      }, {
        /* Characteristic: List programs */
        .uuid = BLE_UUID128_DECLARE(GATT_OTA_CHR_DATA),
        .access_cb = gatt_svr_chr_ota_data_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
        .val_handle = &ota_data_val_handle,
      }, {
        0, /* No more characteristics in this service */
      },
    }
  }, 

  {
    0, /* No more services */
  },
};

static inline int gatt_svr_chr_write(struct os_mbuf *om, uint16_t max_len, void *dst) {
  uint16_t om_len;
  int rc;

  om_len = OS_MBUF_PKTLEN(om);
  if (om_len > max_len) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  rc = ble_hs_mbuf_to_flat(om, dst, max_len, NULL);
  if (rc != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int gatt_svr_chr_opl_msg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  opl_msg_t msg;
  memset(&msg, 0, sizeof(opl_msg_t));

  int rc = gatt_svr_chr_write(ctxt->om, sizeof(opl_msg_t), &msg);
  if (rc != 0) {
    return rc;
  }

  opl_srv_queue_msg(&msg);

  return 0;
}

static int gatt_svr_chr_opl_list_prg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  synth_prg_list_t list;
  synth_prg_list(&list);

  if (os_mbuf_append(ctxt->om, &list, 1 + ((list.count & 0x3f) * sizeof(synth_prg_desc_t))) != 0) {
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return 0;
}

static int gatt_svr_chr_opl_program(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    synth_prg_dump_t prg;
    synth_prg_dump(&prg);
    if (os_mbuf_append(ctxt->om, &prg, sizeof(synth_prg_dump_t)) != 0) {
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
  } else {
    synth_prg_desc_t prg_desc;
    memset(&prg_desc, 0, sizeof(synth_prg_desc_t));
    
    int rc = gatt_svr_chr_write(ctxt->om, sizeof(synth_prg_desc_t), &prg_desc);
    if (rc != 0) {
      return rc;
    }
    
    if (synth_prg_write(&prg_desc) != ESP_OK) {
      return BLE_ATT_ERR_UNLIKELY;
    }
  }

  return 0;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  char buf[BLE_UUID_STR_LEN];

  switch (ctxt->op) {
  case BLE_GATT_REGISTER_OP_SVC:
    MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
    break;

  case BLE_GATT_REGISTER_OP_CHR:
    MODLOG_DFLT(DEBUG, "registering characteristic %s with def_handle=%d val_handle=%d\n", ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
    break;

  case BLE_GATT_REGISTER_OP_DSC:
    MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
    break;

  default:
    assert(0);
    break;
  }
}

static void update_ota_control(uint16_t conn_handle) {
  struct os_mbuf *om;
  esp_err_t err;

  switch (gatt_svr_chr_ota_control_val) {
    case SVR_CHR_OTA_CONTROL_REQUEST:
      ESP_LOGI(TAG, "OTA has been requested via BLE.");
      update_partition = esp_ota_get_next_update_partition(NULL);
      err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        esp_ota_abort(update_handle);
        gatt_svr_chr_ota_control_val = SVR_CHR_OTA_CONTROL_REQUEST_NAK;
      } else {
        gatt_svr_chr_ota_control_val = SVR_CHR_OTA_CONTROL_REQUEST_ACK;
        updating = true;

        packet_size = (gatt_svr_chr_ota_data_val[1] << 8) + gatt_svr_chr_ota_data_val[0];
        ESP_LOGI(TAG, "Packet size is: %d", packet_size);

        num_pkgs_received = 0;
      }

      om = ble_hs_mbuf_from_flat(&gatt_svr_chr_ota_control_val, sizeof(gatt_svr_chr_ota_control_val));
      ble_gattc_notify_custom(conn_handle, ota_control_val_handle, om);
      ESP_LOGI(TAG, "OTA request acknowledgement has been sent.");
      break;
    case SVR_CHR_OTA_CONTROL_DONE:
      updating = false;

      err = esp_ota_end(update_handle);
      if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
          ESP_LOGE(TAG, "Image validation failed, image is corrupted!");
        } else {
          ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
      } else {
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        }
      }

      if (err != ESP_OK) {
        gatt_svr_chr_ota_control_val = SVR_CHR_OTA_CONTROL_DONE_NAK;
      } else {
        gatt_svr_chr_ota_control_val = SVR_CHR_OTA_CONTROL_DONE_ACK;
      }

      om = ble_hs_mbuf_from_flat(&gatt_svr_chr_ota_control_val, sizeof(gatt_svr_chr_ota_control_val));
      ble_gattc_notify_custom(conn_handle, ota_control_val_handle, om);
      ESP_LOGI(TAG, "OTA DONE acknowledgement has been sent.");

      // restart the ESP to finish the OTA
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Preparing to restart!");
        vTaskDelay(pdMS_TO_TICKS(REBOOT_DEEP_SLEEP_TIMEOUT));
        esp_restart();
      }

      break;

    default:
      break;
  }
}

static int gatt_svr_chr_ota_control_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  int rc;
  uint8_t length = sizeof(gatt_svr_chr_ota_control_val);

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      rc = os_mbuf_append(ctxt->om, &gatt_svr_chr_ota_control_val, length);
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      rc = gatt_svr_chr_write(ctxt->om, length, &gatt_svr_chr_ota_control_val);
      update_ota_control(conn_handle);
      return rc;
      break;
    default:
      break;
  }

  // this shouldn't happen
  assert(0);
  return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_svr_chr_ota_data_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  int rc;
  esp_err_t err;

  rc = gatt_svr_chr_write(ctxt->om, sizeof(gatt_svr_chr_ota_data_val), gatt_svr_chr_ota_data_val);

  if (updating) {
    err = esp_ota_write(update_handle, (const void *)gatt_svr_chr_ota_data_val, packet_size);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed (%s)!", esp_err_to_name(err));
    }

    num_pkgs_received++;
    ESP_LOGI(TAG, "Received packet %d", num_pkgs_received);
  }

  return rc;
}

int gatt_svr_init(void) {
  int rc;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_dis_init();

  rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) {
    return rc;
  }

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) {
    return rc;
  }

  return 0;
}

void print_bytes(const uint8_t *bytes, int len) {
  int i;
  for (i = 0; i < len; i++) {
    MODLOG_DFLT(INFO, "%s0x%02x", i != 0 ? ":" : "", bytes[i]);
  }
}

void print_addr(const void *addr) {
  const uint8_t *u8p;

  u8p = addr;
  MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x", u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

static void ble_synth_prph_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  struct ble_hs_adv_fields scan_response_fields;

  int rc;

  memset(&fields, 0, sizeof(fields));
  memset(&scan_response_fields, 0, sizeof scan_response_fields);

  scan_response_fields.name = (uint8_t *)model_num;
  scan_response_fields.name_len = strlen(model_num);
  scan_response_fields.name_is_complete = 1;
  rc = ble_gap_adv_rsp_set_fields(&scan_response_fields);

  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error setting scan response data; rc=%d\n", rc);
    return;
  }

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  fields.uuids128 = (ble_uuid128_t[]) {
    BLE_UUID128_INIT(GATT_OPL_UUID)
  };
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
    return;
  }

  /* Begin advertising */
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(ble_synth_prph_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_synth_prph_gap_event, NULL);
  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
    return;
  }
}

void ble_synth_prph_tx_stop(void) {

}

void ble_synth_prph_tx_reset(void) {

}

void ble_synth_on_disconnect(uint16_t conn_handle) {

}

void ble_synth_subscribe(uint16_t conn_handle, uint16_t attr_handle) {

}

static int ble_synth_prph_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    /* A new connection was established or a connection attempt failed */
    MODLOG_DFLT(INFO, "connection %s; status=%d\n", event->connect.status == 0 ? "established" : "failed", event->connect.status);

    if (event->connect.status != 0) {
      ble_synth_prph_advertise();
    }
    conn_handle = event->connect.conn_handle;
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    MODLOG_DFLT(INFO, "disconnect; reason=%d\n", event->disconnect.reason);

    ble_synth_prph_advertise();
    ble_synth_prph_tx_stop();

    ble_synth_on_disconnect(event->disconnect.conn.conn_handle);
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
      MODLOG_DFLT(INFO, "adv complete\n");
      ble_synth_prph_advertise();
      break;
  case BLE_GAP_EVENT_SUBSCRIBE:
    MODLOG_DFLT(INFO, "subscribe event; cur_notify=%d\n value handle; val_handle=%d\n", event->subscribe.cur_notify, event->subscribe.attr_handle);

    ble_synth_subscribe(event->subscribe.conn_handle, event->subscribe.attr_handle);

    if (event->subscribe.cur_notify) {
      ble_synth_prph_tx_reset();
    }

    ESP_LOGI("BLE_GAP_SUBSCRIBE_EVENT", "conn_handle from subscribe=%d", conn_handle);
    break;

  case BLE_GAP_EVENT_MTU:
    MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d mtu=%d\n", event->mtu.conn_handle, event->mtu.value);
    break;
  }

  return 0;
}

static void ble_synth_prph_on_sync(void) {
  int rc;

  rc = ble_hs_id_infer_auto(0, &ble_synth_prph_addr_type);
  assert(rc == 0);

  uint8_t addr_val[6] = {0};
  rc = ble_hs_id_copy_addr(ble_synth_prph_addr_type, addr_val, NULL);

  MODLOG_DFLT(INFO, "Device Address: ");
  print_addr(addr_val);
  MODLOG_DFLT(INFO, "\n");

  ble_synth_prph_advertise();
}

static void ble_synth_prph_on_reset(int reason) {
  MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

void ble_synth_prph_host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void gatt_srv_start() {
  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK) {
    MODLOG_DFLT(ERROR, "Failed to init nimble %d \n", ret);
    return;
  }

  /* Initialize the NimBLE host configuration */
  ble_hs_cfg.sync_cb = ble_synth_prph_on_sync;
  ble_hs_cfg.reset_cb = ble_synth_prph_on_reset;

  /* Enable bonding */
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_mitm = 1;

  int rc = gatt_svr_init();
  assert(rc == 0);

  /* Set the default device name */
  rc = ble_svc_gap_device_name_set(model_num);
  assert(rc == 0);

  ble_svc_dis_manufacturer_name_set(manuf_name);
  ble_svc_dis_model_number_set(model_num);

  /* Start the task */
  nimble_port_freertos_init(ble_synth_prph_host_task);  
}