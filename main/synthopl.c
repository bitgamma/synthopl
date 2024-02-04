#include "nvs_flash.h"
#include "gatt_svr.h"
#include "opl_srv.h"
#include "midi_srv.h"
#include "synth.h"

void app_main(void) {
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