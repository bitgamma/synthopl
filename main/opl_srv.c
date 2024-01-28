#include "opl_srv.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define OPL_SRV_STACK_SIZE 2048
#define OPL_SRV_QUEUE_LEN 32
#define OPL_SRV_QUEUE_TIMEOUT_MS 20

static QueueHandle_t msg_queue;
static const char *TAG = "opl_srv";

void opl_srv_run(void *param) {
  ESP_LOGI(TAG, "ready");

  while(1) {
    opl_msg_t msg;
    if (xQueueReceive(msg_queue, &msg, portMAX_DELAY) == pdFALSE) {
      continue;
    }

    switch(msg.cmd) {
      case NOTE_ON:
        ESP_LOGI(TAG, "Note On: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        break;
      case NOTE_OFF:
        ESP_LOGI(TAG, "Note Off: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        break;
      case OPL_CFG:
        ESP_LOGI(TAG, "Global OPL Config: map: %d options: %x", msg.params.opl_cfg.map, msg.params.opl_cfg.trem_vib_deep);
        break;
      case CHANNEL_CFG:
        ESP_LOGI(TAG, "Channel Config: %d", msg.params.channel_cfg.id);
        break;
      case LOAD_PROGRAM:
        ESP_LOGI(TAG, "Load program: %d, bank: %d", msg.params.load_prg.prg, msg.params.load_prg.bank);
        break; 
      default:
        ESP_LOGW(TAG, "unknown command %x", msg.cmd);
        break;
    }
  }
}

void opl_srv_start() {
  msg_queue = xQueueCreate(OPL_SRV_QUEUE_LEN, sizeof(opl_msg_t));
  xTaskCreatePinnedToCore(opl_srv_run, "opl_srv", OPL_SRV_STACK_SIZE, NULL, 10, NULL, 1);
}

void opl_srv_queue_msg(const opl_msg_t* msg) {
  xQueueSend(msg_queue, msg, pdTICKS_TO_MS(OPL_SRV_QUEUE_TIMEOUT_MS));
}