#include <string.h>

#include "opl_srv.h"
#include "opl_bus.h"
#include "synth.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define OPL_SRV_STACK_SIZE 8192
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
#define OPL_OPL3_2OPS_MODE 0x00
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

static const uint8_t OPL_VELOCITY_TO_OUTPUT_LEVEL[64] = {
  0x3f, 0x3a, 0x35, 0x30, 0x2c, 0x29, 0x25, 0x24,
  0x23, 0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c,
  0x1b, 0x1a, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14,
  0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0e, 0x0d,
  0x0d, 0x0c, 0x0c, 0x0b, 0x0b, 0x0a, 0x0a, 0x09,
  0x09, 0x08, 0x08, 0x07, 0x07, 0x06, 0x06, 0x06,
  0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x03,
  0x03, 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x00,
};

enum opl_op_role {
  OP_CARRIER,
  OP_MOD1,
  OP_MOD2,
  OP_MOD3,
};

static const enum opl_op_role OP_ROLE_4OPS[4][4] = {
  {OP_MOD3, OP_MOD2, OP_MOD1, OP_CARRIER},
  {OP_CARRIER, OP_MOD2, OP_MOD1, OP_CARRIER},
  {OP_MOD1, OP_CARRIER, OP_MOD1, OP_CARRIER},
  {OP_CARRIER, OP_MOD1, OP_CARRIER, OP_CARRIER},
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

  opl_bus_write(opl_channel_reg_addr(OPL_CH_CHANNELS_FMF_SYNTH_BASE, opl_ch), (feedback_synth & 0x3f));

  if (op_count == 4) {
    opl_bus_write(opl_channel_reg_addr(OPL_CH_CHANNELS_FMF_SYNTH_BASE, (opl_ch + 3)), ((feedback_synth & 0x3e) | ((feedback_synth & 0x80) >> 7)));
  }
}

static inline uint16_t opl_midi_note_to_fnum(const opl_note_t* note) {
  uint16_t fnum = OPL_NOTE_TO_FNUM[note->note % 12];
  uint16_t octave = (note->note / 12);

  if (!note->drum_channel) {
    fnum += g_synth.pitch_bend;
  }

  // MIDI notes start from -1 octave
  // TODO: actually octave -1 and 0 can both be rendered with block 0, add the fnums if it makes sense
  if (octave < 1) {
    octave = 1;
  } else if (octave > 8) {
    octave = 8;
  }

  return ((octave - 1) << 10) | fnum;
}

static void opl_set_fnum(uint8_t channel, const opl_note_t* note, uint8_t onflag) {
  uint16_t fnum = opl_midi_note_to_fnum(note);
  fnum_cache[channel] = (fnum >> 8);

  opl_bus_write(opl_channel_reg_addr(OPL_CH_FREQL_BASE, channel), (uint8_t) (fnum & 0xff));
  opl_bus_write(opl_channel_reg_addr(OPL_CH_KEYON_BLOCK_FREQH_BASE, channel), onflag | fnum_cache[channel]);  
}

static inline uint8_t opl_is_carrier(uint8_t op, uint8_t op_count, uint8_t synth_mode) {
  if (op_count == 2) {
    // in FM only op 1 is carrier, in AM both are
    return (op | (g_synth.prg.keyboard.ch_feedback_synth & 1));
  } else {
    return OP_ROLE_4OPS[synth_mode][op] == OP_CARRIER;
  }
}

static void opl_note_on(opl_note_t* note) {
  uint8_t voice_ch = synth_add_voice(note);

  uint8_t op_count;
  opl_operator_t* ops;
  uint8_t synth_mode;

  if (note->drum_channel) {
    op_count = 2;
    ops = g_synth.prg.drumkit[voice_ch].ops;
    synth_mode = g_synth.prg.drumkit[voice_ch].ch_feedback_synth & 0x1;
  } else {
    op_count = g_synth.prg.config.map ? 2 : 4;
    ops = g_synth.prg.keyboard.ops;
    synth_mode = (g_synth.prg.keyboard.ch_feedback_synth & 0x80 >> 6) | (g_synth.prg.keyboard.ch_feedback_synth & 1);
  }

  voice_ch = OPL_VOICE_TO_CHANNEL[voice_ch];
  
  for (int i = 0; i < op_count; i++) {
    if (opl_is_carrier(i, op_count, synth_mode)) {
      uint8_t ksl_ol = (ops[i].ksl_output & 0xc0) | (OPL_VELOCITY_TO_OUTPUT_LEVEL[note->velocity >> 1] + (ops[i].ksl_output & 0x3f));
      opl_bus_write(opl_op_reg_addr(OPL_OP_KSL_OUTPUT_BASE, OPL_CHANNEL_OPS[voice_ch][i]), ksl_ol);
    }
  }

  opl_set_fnum(voice_ch, note, OPL_CH_KEY_ON);
}

static void opl_note_off(const opl_note_t* note) {
  uint8_t voice_ch = synth_remove_voice(note);
  if (voice_ch == VOICE_NONE) {
    return;
  }

  voice_ch = OPL_VOICE_TO_CHANNEL[voice_ch];
  opl_bus_write(opl_channel_reg_addr(OPL_CH_KEYON_BLOCK_FREQH_BASE, voice_ch), fnum_cache[voice_ch]);
}

static void opl_load_keyboard() {
  int op_count = g_synth.prg.config.map ? 2 : 4;
  int ch_end = KEYBOARD_POLY_CFG[g_synth.prg.config.map] + DRUMKIT_SIZE;
  for (int i = DRUMKIT_SIZE; i < ch_end; i++) {
    opl_write_channel(OPL_VOICE_TO_CHANNEL[i], g_synth.prg.keyboard.ch_feedback_synth, g_synth.prg.keyboard.ops, op_count);
  }
}

static void opl_cfg(const opl_config_t* cfg) {
  if (g_synth.prg.config.map != cfg->map) {
    g_synth.prg.config.map = cfg->map & 0x1;
    opl_bus_write(OPL_OPL3_CONFIG_ADDR, g_synth.prg.config.map ? OPL_OPL3_2OPS_MODE : OPL_OPL3_4OPS_MODE);
    opl_load_keyboard();
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
    opl_load_keyboard();
  }
}

static void opl_load_prg(const opl_load_prg_t* prg) {
  synth_load_prg(prg);

  opl_bus_write(OPL_OPL3_CONFIG_ADDR, g_synth.prg.config.map ? OPL_OPL3_2OPS_MODE : OPL_OPL3_4OPS_MODE);
  opl_bus_write(OPL_TREM_VIBR_PERCUSSION_ADDR, g_synth.prg.config.trem_vib_deep);
  
  for (int i = 0; i < DRUMKIT_SIZE; i++) {
    opl_write_channel(OPL_VOICE_TO_CHANNEL[i], g_synth.prg.drumkit[i].ch_feedback_synth, g_synth.prg.drumkit[i].ops, 2);
  }

  opl_load_keyboard();
}

void opl_pitch_bend(int16_t bend) {
  g_synth.pitch_bend = bend;
  opl_note_t note = {.drum_channel = 0, .velocity = 127};
  for (int i = 0; i < KEYBOARD_POLY_CFG[g_synth.prg.config.map]; i++) {
    note.note = g_synth.keyboard_voices[i].note & 0x7f;
    uint8_t onflag = ((~g_synth.keyboard_voices[i].note) & SYNTH_NOTE_OFF) >> 2;
    opl_set_fnum(OPL_VOICE_TO_CHANNEL[DRUMKIT_SIZE + i], &note, onflag);
  }
}

void opl_srv_run(void *param) {
  ESP_LOGI(TAG, "ready");

  opl_bus_write(OPL_OPL3_ENABLE_ADDR, OPL_OPL3_ENABLE);
  const opl_load_prg_t prg = { .bank = 0, .prg = 0 };
  opl_load_prg(&prg);

  while(1) {
    opl_msg_t msg;
    if (xQueueReceive(msg_queue, &msg, portMAX_DELAY) == pdFALSE) {
      continue;
    }

    switch(msg.cmd) {
      case NOTE_ON:
        ESP_LOGD(TAG, "Note On: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        opl_note_on(&msg.params.note);
        break;
      case NOTE_OFF:
        ESP_LOGD(TAG, "Note Off: %d ch: %d", msg.params.note.note, msg.params.note.drum_channel);
        opl_note_off(&msg.params.note);
        break;
      case OPL_CFG:
        ESP_LOGD(TAG, "Global OPL Config: map: %d options: %x", msg.params.opl_cfg.map, msg.params.opl_cfg.trem_vib_deep);
        opl_cfg(&msg.params.opl_cfg);
        break;
      case CHANNEL_CFG:
        ESP_LOGD(TAG, "Channel Config: %d", msg.params.channel_cfg.id);
        opl_channel_cfg(&msg.params.channel_cfg);
        break;
      case LOAD_PROGRAM:
        ESP_LOGD(TAG, "Load Program: %d, bank: %d", msg.params.load_prg.prg, msg.params.load_prg.bank);
        opl_load_prg(&msg.params.load_prg);
        break; 
      case DRUMKIT_NOTES:
        ESP_LOGD(TAG, "Set drumkit notes");
        memcpy(g_synth.prg.drumkit_notes, msg.params.drumkit_notes, DRUMKIT_SIZE);
        break;
      case PITCH_BEND:
        ESP_LOGD(TAG, "Pitch bend: %d", msg.params.bend);
        opl_pitch_bend(msg.params.bend);
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