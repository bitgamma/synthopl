#ifndef __SYNTH__
#define __SYNTH__

#include <stdint.h>
#include "opl_srv.h"

#define KEYBOARD_MAX_POLY 12
#define NOTE_OFF 0x80
#define VOICE_NONE NOTE_OFF

typedef struct {
  uint64_t last_modified;
  uint8_t note;
} voice_t;

typedef struct {
  uint8_t bank_num;
  uint8_t prg_num;
  uint8_t drumkit_voices[DRUMKIT_SIZE];
  voice_t keyboard_voices[KEYBOARD_MAX_POLY];
  opl_program_t prg;
} synth_t;

extern synth_t g_synth;

uint8_t synth_add_voice(opl_note_t* note);
uint8_t synth_remove_voice(opl_note_t* note);

#endif