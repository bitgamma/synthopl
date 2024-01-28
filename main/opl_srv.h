#ifndef __OPL_SRV__
#define __OPL_SRV__

#include <stdint.h>

#define PROFILE_MAX_NAME_LEN 12
#define DRUMKIT_SIZE 6

typedef enum __attribute__ ((packed)) {
  NOTE_ON,
  NOTE_OFF,
  OPL_CFG,
  CHANNEL_CFG,
  LOAD_PROGRAM
} opl_cmd_t;

typedef enum __attribute__ ((packed)) {
  KEYBOARD_4OPS,
  KEYBOARD_2OPS
} opl_map_t;

typedef enum __attribute__ ((packed)) {
  BASS_DRUM,
  SNARE_DRUM,
  TOM,
  CYMBAL,
  HIHAT,
  EXTRA,
  KEYBOARD,
} opl_channel_id_t;

typedef struct __attribute__ ((packed)) {
  uint8_t note;
  uint8_t velocity;
  uint8_t drum_channel;
} opl_note_t;

typedef struct __attribute__ ((packed)) {
  opl_map_t map;
  uint8_t trem_vib_deep; 
} opl_config_t;

typedef struct __attribute__ ((packed)) {
  uint8_t trem_vibr_sust_ksr_fmf;
  uint8_t ksl_output;
  uint8_t attack_decay;
  uint8_t sustain_release;
  uint8_t waveform;
} opl_operator_t;

typedef struct __attribute__ ((packed)) {
  uint8_t ch_feedback_synth;
  opl_operator_t ops[2];
} opl_2ops_channel_t;

typedef struct __attribute__ ((packed)) {
  uint8_t ch_feedback_synth;
  opl_operator_t ops[4];
} opl_4ops_channel_t;

typedef struct __attribute__ ((packed)) {
  opl_channel_id_t id;
  opl_4ops_channel_t channel;
} opl_channel_cfg_t;

typedef struct __attribute__((packed)) {
  uint8_t bank;
  uint8_t prg;
} opl_load_prg_t;

typedef struct __attribute__ ((packed)) {
  opl_cmd_t cmd;
  union {
    opl_note_t note;
    opl_config_t opl_cfg;
    opl_channel_cfg_t channel_cfg;
    opl_load_prg_t load_prg;
  } params;
} opl_msg_t;

typedef struct __attribute__ ((packed)) {
  char name[PROFILE_MAX_NAME_LEN];
  opl_config_t config;
  opl_4ops_channel_t keyboard;
  opl_2ops_channel_t drumkit[DRUMKIT_SIZE];
} opl_program_t;

void opl_srv_start();
void opl_srv_queue_msg(const opl_msg_t* msg);

#endif