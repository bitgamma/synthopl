#include "opl_srv.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define OPL_SRV_STACK_SIZE 2048
#define OPL_SRV_QUEUE_LEN 32
#define OPL_SRV_QUEUE_TIMEOUT_MS 20

#define OPL_CHANNEL_COUNT 18
#define OPL_NO_OP 0xff
#define OPL_NO_OPS OPL_NO_OP, OPL_NO_OP

struct opl_channel_ops {
  uint8_t op1;
  uint8_t op2;
  uint8_t op3;
  uint8_t op4;
};

const uint8_t OPL_VOICE_TO_CHANNEL[OPL_CHANNEL_COUNT] = { 6, 7, 8, 15, 16, 17, 0, 1, 2, 9, 10, 11, 3, 4, 5, 12, 13, 14 };
const struct opl_channel_ops OPL_CHANNEL_OPS[OPL_CHANNEL_COUNT] = { 
  {0, 3, 6, 9},
  {1, 4, 7, 10},
  {2, 5, 8, 11},
  {6, 9, OPL_NO_OPS},
  {7, 10, OPL_NO_OPS},
  {8, 11, OPL_NO_OPS},
  {12, 15, OPL_NO_OPS},
  {13, 16, OPL_NO_OPS},
  {14, 17, OPL_NO_OPS},
  {18, 21, 24, 27},
  {19, 22, 25, 28},
  {20, 23, 26, 29},
  {24, 27, OPL_NO_OPS},
  {25, 28, OPL_NO_OPS},
  {26, 29, OPL_NO_OPS},
  {30, 33, OPL_NO_OPS},
  {31, 34, OPL_NO_OPS},
  {32, 35, OPL_NO_OPS}
};

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
        ESP_LOGI(TAG, "Load Program: %d, bank: %d", msg.params.load_prg.prg, msg.params.load_prg.bank);
        break; 
      default:
        ESP_LOGW(TAG, "Unknown Command %x", msg.cmd);
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