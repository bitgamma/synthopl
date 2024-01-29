#include "opl_srv.h"
#include "opl_bus.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define OPL_SRV_STACK_SIZE 2048
#define OPL_SRV_QUEUE_LEN 32
#define OPL_SRV_QUEUE_TIMEOUT_MS 20

#define OPL_CHANNEL_COUNT 18
#define OPL_OP_COUNT_BANK 18
#define OPL_NO_OP 0xff
#define OPL_NO_OPS OPL_NO_OP, OPL_NO_OP

#define OPL_OP_TREM_VIBR_SUST_KSR_FMF_BASE 0x20
#define OPL_OP_KSL_OUTPUT_BASE 0x40
#define OPL_OP_ATTACK_DECAY_BASE 0x60
#define OPL_OP_SUSTAIN_RELEASE_BASE 0x80
#define OPL_OP_WAVEFORM_BASE 0xe0

#define OPL_CH_FREQL_BASE 0xa0
#define OPL_CH_KEYON_BLOCK_FREQH_BASE 0xb0
#define OPL_CH_CHANNELS_FMF_SYNTH_BASE 0xc0

#define OPL_OPL3_CONFIG 0x8004
#define OPL_OPL3_ENABLE 0x8005
#define OPL_TREM_VIBR_PERCUSSION 0x00bd

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

const uint8_t OPL_OP_REG_OFF[OPL_OP_COUNT_BANK] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };

static QueueHandle_t msg_queue;
static const char *TAG = "opl_srv";

static inline uint16_t opl_channel_reg_addr(uint8_t base, uint8_t ch) {
  uint16_t hi;

  if (ch >= (OPL_CHANNEL_COUNT/2)) {
    hi = 0x8000;
    ch -= (OPL_CHANNEL_COUNT/2);
  } else {
    hi = 0;
  }

  return hi | (base + ch);
}

static inline uint16_t opl_op_reg_addr(uint8_t base, uint8_t op) {
  uint16_t hi;

  if (op >= OPL_OP_COUNT_BANK) {
    hi = 0x8000;
    op -= OPL_OP_COUNT_BANK;
  } else {
    hi = 0;
  }

  return hi | (base + OPL_OP_REG_OFF[op]);
}

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
  opl_bus_init();
  msg_queue = xQueueCreate(OPL_SRV_QUEUE_LEN, sizeof(opl_msg_t));
  xTaskCreatePinnedToCore(opl_srv_run, "opl_srv", OPL_SRV_STACK_SIZE, NULL, 10, NULL, 1);
}

void opl_srv_queue_msg(const opl_msg_t* msg) {
  xQueueSend(msg_queue, msg, pdTICKS_TO_MS(OPL_SRV_QUEUE_TIMEOUT_MS));
}