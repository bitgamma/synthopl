#include <string.h>

#include "opl_srv.h"
#include "opl_bus.h"
#include "synth.h"
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

#define OPL_CH_KEY_ON 0x20

#define OPL_OPL3_CONFIG_ADDR 0x8004
#define OPL_OPL3_ENABLE_ADDR 0x8005
#define OPL_TREM_VIBR_PERCUSSION_ADDR 0x00bd

#define OPL_OPL3_4OPS_MODE 0x3f
#define OPL_OPL3_2OPS_MODE
#define OPL_OPL3_ENABLE 0x01

static const uint8_t OPL_VOICE_TO_CHANNEL[OPL_CHANNEL_COUNT] = { 6, 7, 8, 15, 16, 17, 0, 1, 2, 9, 10, 11, 3, 4, 5, 12, 13, 14 };
static const uint8_t OPL_CHANNEL_OPS[OPL_CHANNEL_COUNT][4] = { 
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

static const uint8_t OPL_OP_REG_OFF[OPL_OP_COUNT_BANK] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };

static const uint16_t OPL_NOTE_TO_FNUM[12] = {
	345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651
};

static const char *TAG = "opl_srv";

static QueueHandle_t msg_queue;
static uint8_t fnum_cache[OPL_CHANNEL_COUNT];

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

static void opl_write_channel(uint8_t opl_ch, uint8_t feedback_synth, const opl_operator_t *ops, size_t op_count) {
  for (int i = 0; i < op_count; i++) {
    uint8_t op_id = OPL_CHANNEL_OPS[opl_ch][i];
    opl_bus_write(opl_op_reg_addr(OPL_OP_TREM_VIBR_SUST_KSR_FMF_BASE, op_id), ops[i].trem_vibr_sust_ksr_fmf);
    opl_bus_write(opl_op_reg_addr(OPL_OP_KSL_OUTPUT_BASE, op_id), ops[i].ksl_output);
    opl_bus_write(opl_op_reg_addr(OPL_OP_ATTACK_DECAY_BASE, op_id), ops[i].attack_decay);
    opl_bus_write(opl_op_reg_addr(OPL_OP_SUSTAIN_RELEASE_BASE, op_id), ops[i].sustain_release);
    opl_bus_write(opl_op_reg_addr(OPL_OP_WAVEFORM_BASE, op_id), ops[i].waveform);
  }

  opl_bus_write(opl_channel_reg_addr(OPL_CH_CHANNELS_FMF_SYNTH_BASE, opl_ch), feedback_synth);
}

static inline uint16_t opl_midi_note_to_fnum(const opl_note_t* note) {
  //TODO: consider velocity/pitch bend/detune/tune
  uint16_t fnum = OPL_NOTE_TO_FNUM[note->note % 12];
  uint16_t octave = (note->note / 12);

  // MIDI notes start from -1 octave
  if (octave < 1) {
    octave = 1;
  } else if (octave > 8) {
    octave = 8;
  }

  return ((octave - 1) << 10) | fnum;
}

static void opl_note_on(opl_note_t* note) {
  uint8_t voice_ch = synth_add_voice(note);
  if (voice_ch == VOICE_NONE) {
    return;
  }

  uint16_t fnum = opl_midi_note_to_fnum(note);
  fnum_cache[voice_ch] = (fnum >> 8);

  voice_ch = OPL_VOICE_TO_CHANNEL[voice_ch];
  opl_bus_write(opl_channel_reg_addr(OPL_CH_FREQL_BASE, voice_ch), (uint8_t) (fnum & 0xff));
  opl_bus_write(opl_channel_reg_addr(OPL_CH_KEYON_BLOCK_FREQH_BASE, voice_ch), OPL_CH_KEY_ON | fnum_cache[voice_ch]);
}

static void opl_note_off(const opl_note_t* note) {
  uint8_t voice_ch = synth_remove_voice(note);
  if (voice_ch == VOICE_NONE) {
    return;
  }

  voice_ch = OPL_VOICE_TO_CHANNEL[voice_ch];
  opl_bus_write(opl_channel_reg_addr(OPL_CH_KEYON_BLOCK_FREQH_BASE, voice_ch), fnum_cache[voice_ch]);
}

static void opl_cfg(const opl_config_t* cfg) {
  if (g_synth.prg.config.map != cfg->map) {
    //TODO: when switching mode the keyboard channels must be reconfigured. Maybe is not a bad idea to 
    // automatically reconfigure them to provide a fallback in case the client doesn't do it.
    g_synth.prg.config.map = cfg->map & 0x1;
    opl_bus_write(OPL_OPL3_CONFIG_ADDR, g_synth.prg.config.map ? OPL_OPL3_2OPS_MODE : OPL_OPL3_4OPS_MODE);
  }

  g_synth.prg.config.trem_vib_deep = cfg->trem_vib_deep & 0xc0;
  opl_bus_write(OPL_TREM_VIBR_PERCUSSION_ADDR, g_synth.prg.config.trem_vib_deep);
}

static void opl_channel_cfg(const opl_channel_cfg_t* ch_cfg) {
  if (ch_cfg->id > KEYBOARD) {
    ESP_LOGW(TAG, "Invalid channel id: %d", ch_cfg->id);
    return;
  }

  if (ch_cfg->id != KEYBOARD) {
    memcpy(&g_synth.prg.drumkit[ch_cfg->id], &ch_cfg->channel, sizeof(opl_2ops_channel_t));
    opl_write_channel(OPL_VOICE_TO_CHANNEL[ch_cfg->id], ch_cfg->channel.ch_feedback_synth, ch_cfg->channel.ops, 2);
  } else {
    memcpy(&g_synth.prg.keyboard, &ch_cfg->channel, sizeof(opl_4ops_channel_t));
    int op_count = g_synth.prg.config.map ? 2 : 4;
    int ch_end = KEYBOARD_POLY_CFG[g_synth.prg.config.map] + DRUMKIT_SIZE;
    for (int i = DRUMKIT_SIZE; i < ch_end; i++) {
      opl_write_channel(OPL_VOICE_TO_CHANNEL[i], ch_cfg->channel.ch_feedback_synth, ch_cfg->channel.ops, op_count);
    }
  }
}

static void opl_load_prg(const opl_load_prg_t* prg) {
  //TODO: implement
}

void opl_srv_run(void *param) {
  ESP_LOGI(TAG, "ready");

  opl_bus_write(OPL_OPL3_ENABLE_ADDR, OPL_OPL3_ENABLE);
  opl_bus_write(OPL_OPL3_CONFIG_ADDR, OPL_OPL3_4OPS_MODE);

  while(1) {
    opl_msg_t msg;
    if (xQueueReceive(msg_queue, &msg, portMAX_DELAY) == pdFALSE) {
      continue;
    }

    switch(msg.cmd) {
      case NOTE_ON:
        ESP_LOGI(TAG, "Note On: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        opl_note_on(&msg.params.note);
        break;
      case NOTE_OFF:
        ESP_LOGI(TAG, "Note Off: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        opl_note_off(&msg.params.note);
        break;
      case OPL_CFG:
        ESP_LOGI(TAG, "Global OPL Config: map: %d options: %x", msg.params.opl_cfg.map, msg.params.opl_cfg.trem_vib_deep);
        opl_cfg(&msg.params.opl_cfg);
        break;
      case CHANNEL_CFG:
        ESP_LOGI(TAG, "Channel Config: %d", msg.params.channel_cfg.id);
        opl_channel_cfg(&msg.params.channel_cfg);
        break;
      case LOAD_PROGRAM:
        ESP_LOGI(TAG, "Load Program: %d, bank: %d", msg.params.load_prg.prg, msg.params.load_prg.bank);
        opl_load_prg(&msg.params.load_prg);
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