#include "synth.h"
#include "esp_timer.h"

const int KEYBOARD_POLY_CFG[2] = { 6, 12 };

synth_t g_synth;

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