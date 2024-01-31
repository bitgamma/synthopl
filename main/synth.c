#include <string.h>

#include "synth.h"
#include "esp_timer.h"
#include "nvs.h"

const char* const HEX_DIGITS = "0123456789abcdef";
const int KEYBOARD_POLY_CFG[2] = { 6, 12 };

synth_t g_synth;

static inline uint8_t base16_hexlet_decode(char c) {
  if ((c >= '0') && (c <= '9')) {
    return c - '0';
  } else if ((c >= 'a') && (c <= 'f')) {
    return 10 + (c - 'a');
  } else if ((c >= 'A') && (c <= 'F')) {
    return 10 + (c - 'A');
  }

  return 0xff;
}

static uint8_t synth_add_drumkit_voice(opl_note_t* note) {
  uint8_t ch = note->note & 0x7;

  if (ch >= DRUMKIT_SIZE) {
    return VOICE_NONE;
  }

  g_synth.drumkit_voices |= (1 << ch);
  note->note = g_synth.prg.drumkit_notes[ch];

  return ch; 
}

static uint8_t synth_add_keyboard_voice(const opl_note_t* note) {
  // TODO: evaluate note stealing algorithms
  int voice = VOICE_NONE;

  for (int i = 0; i < KEYBOARD_POLY_CFG[g_synth.prg.config.map]; i++) {
    if (g_synth.keyboard_voices[i].note & SYNTH_NOTE_OFF) {
      if ((voice == VOICE_NONE) || 
        (g_synth.keyboard_voices[i].last_modified < g_synth.keyboard_voices[voice].last_modified)) {
        voice = i;
      }
    }
  }

  if (voice != VOICE_NONE) {
    g_synth.keyboard_voices[voice].last_modified = esp_timer_get_time();
    g_synth.keyboard_voices[voice].note = note->note;
    return DRUMKIT_SIZE + voice;
  }

  return VOICE_NONE;
}

uint8_t synth_add_voice(opl_note_t* note) {
  if (note->drum_channel) {
    return synth_add_drumkit_voice(note);
  } else {
    return synth_add_keyboard_voice(note);
  }
}

static uint8_t synth_remove_drumkit_voice(const opl_note_t* note) {
  uint8_t ch = note->note & 0x7;

  if (ch >= DRUMKIT_SIZE) {
    return VOICE_NONE;
  }

  if (g_synth.drumkit_voices & (1 << ch)) {
    g_synth.drumkit_voices &= ~(1 << ch); 
    return ch;
  }

  return VOICE_NONE; 
}

static uint8_t synth_remove_keyboard_voice(const opl_note_t* note) {
  for (int i = 0; i < KEYBOARD_POLY_CFG[g_synth.prg.config.map]; i++) {
    if (g_synth.keyboard_voices[i].note == note->note) {
      g_synth.keyboard_voices[i].last_modified = esp_timer_get_time();
      g_synth.keyboard_voices[i].note |= SYNTH_NOTE_OFF;
      return DRUMKIT_SIZE + i;
    }
  }

  return VOICE_NONE;
}

uint8_t synth_remove_voice(const opl_note_t* note) {
  if (note->drum_channel) {
    return synth_remove_drumkit_voice(note);
  } else {
    return synth_remove_keyboard_voice(note);
  }
}

static void prg_to_key(uint8_t bank, uint8_t prg, char key[5]) {
  key[0] = HEX_DIGITS[bank >> 4];
  key[1] = HEX_DIGITS[bank & 0xf];
  key[2] = HEX_DIGITS[prg >> 4];
  key[3] = HEX_DIGITS[prg & 0xf]; 
  key[4] = '\0';
}

void synth_load_prg(const opl_load_prg_t* prg) {
  char key[5];
  prg_to_key(prg->bank, prg->prg, key);
  size_t len = sizeof(opl_program_t);
  if (nvs_get_blob(g_synth.storage, key, &g_synth.prg, &len) != ESP_OK) {
    memset(&g_synth.prg, 0, sizeof(opl_program_t));
  }

  g_synth.bank_num = prg->bank;
  g_synth.prg_num = prg->prg;
}

void synth_prg_dump(synth_prg_dump_t* out) {
  out->bank_num = g_synth.bank_num;
  out->prg_num = g_synth.prg_num;
  memcpy(&out->prg, &g_synth.prg, sizeof(opl_program_t));
}

esp_err_t synth_prg_write(const synth_prg_desc_t* prg_desc) {
  memcpy(&g_synth.prg.name, &prg_desc->prg_name, PROGRAM_MAX_NAME_LEN);
  
  char key[5];
  prg_to_key(prg_desc->bank_num, prg_desc->prg_num, key);
  if (nvs_set_blob(g_synth.storage, key, &g_synth.prg, sizeof(opl_program_t)) != ESP_OK) {
    return ESP_FAIL;
  }

  g_synth.bank_num = prg_desc->bank_num;
  g_synth.prg_num = prg_desc->prg_num;

  return ESP_OK;
}