#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOSConfig.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "gatt_svr.h"

static const char *manuf_name = "Bitgamma";
static const char *model_num = "Synth OPL";
static const char *system_id = "SYNO";
static const char *device_name = "synth_opl";

static uint16_t conn_handle;
static uint8_t ble_synth_prph_addr_type;

static int ble_synth_prph_gap_event(struct ble_gap_event *event, void *arg);
static int gatt_svr_chr_access_device_info(uint16_t conn_handle, uint16_t attr_handle,struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
  {
    /* Service: Device Information */
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID16_DECLARE(GATT_DIS_DEVICE_INFO_UUID),
    .characteristics = (struct ble_gatt_chr_def[]) { 
      {
        /* Characteristic: * Manufacturer name */
        .uuid = BLE_UUID16_DECLARE(GATT_DIS_CHR_UUID16_MFC_NAME),
        .access_cb = gatt_svr_chr_access_device_info,
        .flags = BLE_GATT_CHR_F_READ,
      }, {
        /* Characteristic: Model number string */
        .uuid = BLE_UUID16_DECLARE(GATT_DIS_CHR_UUID16_MODEL_NO),
        .access_cb = gatt_svr_chr_access_device_info,
        .flags = BLE_GATT_CHR_F_READ,
      }, {
        /* Characteristic: System ID */
        .uuid = BLE_UUID16_DECLARE(GATT_DIS_CHR_UUID16_SYS_ID),
        .access_cb = gatt_svr_chr_access_device_info,
        .flags = BLE_GATT_CHR_F_READ,
      }, {
        0, /* No more characteristics in this service */
      },
    }
  },

  {
    0, /* No more services */
  },
};

static int gatt_svr_chr_access_device_info(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  uint16_t uuid;
  int rc;

  uuid = ble_uuid_u16(ctxt->chr->uuid);

  if (uuid == GATT_DIS_CHR_UUID16_MODEL_NO) {
    rc = os_mbuf_append(ctxt->om, model_num, strlen(model_num));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (uuid == GATT_DIS_CHR_UUID16_MFC_NAME) {
    rc = os_mbuf_append(ctxt->om, manuf_name, strlen(manuf_name));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (uuid == GATT_DIS_CHR_UUID16_SYS_ID) {
    rc = os_mbuf_append(ctxt->om, system_id, strlen(system_id));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return BLE_ATT_ERR_UNLIKELY;
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

int gatt_svr_init(void) {
  int rc;

  ble_svc_gap_init();
  ble_svc_gatt_init();

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
  int rc;

  /*
    *  Set the advertisement data included in our advertisements:
    *     o Flags (indicates advertisement type and other general info)
    *     o Advertising tx power
    *     o Device name
    */
  memset(&fields, 0, sizeof(fields));

  /*
    * Advertise two flags:
    *      o Discoverability in forthcoming advertisement (general)
    *      o BLE-only (BR/EDR unsupported)
    */
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  /*
    * Indicate that the TX power level field should be included; have the
    * stack fill this value automatically.  This is done by assigning the
    * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
    */
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  fields.uuids16 = (ble_uuid16_t[]) {
    //BLE_UUID16_INIT(BLE_SVC_HTP_UUID16)
    BLE_UUID16_INIT(0)
  };
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

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

void ble_synth_prph_tx_htp_stop(void) {

}

void ble_synth_prph_tx_htp_reset(void) {

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
    ble_synth_prph_tx_htp_stop();

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
      ble_synth_prph_tx_htp_reset();
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
  rc = ble_svc_gap_device_name_set(device_name);
  assert(rc == 0);

  /* Start the task */
  nimble_port_freertos_init(ble_synth_prph_host_task);  
}