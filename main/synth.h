#ifndef __SYNTH__
#define __SYNTH__

#include <stdint.h>
#include "opl_srv.h"
#include "nvs_flash.h"

#define KEYBOARD_MAX_POLY 12
#define SYNTH_NOTE_OFF 0x80
#define VOICE_NONE SYNTH_NOTE_OFF
#define DESCRIPTOR_MAX_COUNT 20
#define SYNTH_DESC_LIST_FIRST 0x40
#define SYNTH_DESC_LIST_LAST 0x80

extern const int KEYBOARD_POLY_CFG[2];

typedef struct {
  uint64_t last_modified;
  uint8_t note;
} voice_t;

typedef struct {
  nvs_handle_t storage;
  nvs_iterator_t prg_list_it;
  uint8_t bank_num;
  uint8_t prg_num;
  uint8_t drumkit_voices;
  voice_t keyboard_voices[KEYBOARD_MAX_POLY];
  opl_program_t prg;
} synth_t;

typedef struct __attribute__((packed)) {
  uint8_t bank_num;
  uint8_t prg_num;
  opl_program_t prg;
} synth_prg_dump_t;

typedef struct __attribute__((packed)) {
  uint8_t bank_num;
  uint8_t prg_num;
  char prg_name[PROGRAM_MAX_NAME_LEN];
} synth_prg_desc_t;

typedef struct __attribute__((packed)) {
  uint8_t count;
  synth_prg_desc_t descriptors[DESCRIPTOR_MAX_COUNT];
} synth_prg_list_t;

extern synth_t g_synth;

void synth_init();
uint8_t synth_add_voice(opl_note_t* note);
uint8_t synth_remove_voice(const opl_note_t* note);
void synth_load_prg(const opl_load_prg_t* prg);
void synth_prg_dump(synth_prg_dump_t* out);
esp_err_t synth_prg_write(const synth_prg_desc_t* prg_desc);
void synth_prg_list(synth_prg_list_t* out);
#endif