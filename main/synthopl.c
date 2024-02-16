#include "nvs_flash.h"
#include "gatt_svr.h"
#include "opl_srv.h"
#include "midi_srv.h"
#include "synth.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

static const char *TAG = "synthopl";

static bool perform_upgrade() {
  // Add data format migration here
  return true;
}

void app_main(void) {
  const esp_partition_t *partition = esp_ota_get_running_partition();

  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(TAG, "An OTA update has been detected.");
      if (perform_upgrade()) {
        ESP_LOGI(TAG, "Upgrade completed successfully! Continuing execution.");
        esp_ota_mark_app_valid_cancel_rollback();
      } else {
        ESP_LOGE(TAG, "Upgrade failed! Start rollback to the previous version.");
        esp_ota_mark_app_invalid_rollback_and_reboot();
      }
    }
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  synth_init();  
  opl_srv_start();
  midi_srv_start();
  gatt_srv_start();
}