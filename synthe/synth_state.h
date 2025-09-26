#pragma once

#include "config.h"

#include <Arduino.h>
#include <MozziHeadersOnly.h>
#include <Adafruit_MCP23X17.h>
#include <ResonantFilter.h>
#include <Oscil.h>
#include <Phasor.h>
#include <U8g2lib.h>
#include <Wire.h>

#include <ADSR.h>

#include <tables/sin2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>

struct SynthParams {
  float pitchOffset = 0.0f;
  float waveMorph = 0.0f;
  float envAttack = 25.0f;
  float envSustain = 0.8f;
  float envRelease = 300.0f;
  float lfoRate = 4.0f;
  float lfoDepthPitch = 0.3f;
  float lfoDepthFilter = 200.0f;
  float filterCutoff = 1200.0f;
  float filterResonance = 0.7f;
  float masterGain = 0.7f;
};

constexpr uint8_t ANALOG_INPUT_COUNT = 6;
extern const uint8_t analogPins[ANALOG_INPUT_COUNT];

#if defined(ARDUINO_ARCH_STM32)
constexpr float ANALOG_MAX_VALUE = 4095.0f;
#else
constexpr float ANALOG_MAX_VALUE = 1023.0f;
#endif

#if defined(KEYBOARD_DRIVER_MCP23017)
constexpr uint8_t MCP_KEYBOARD_ADDR = 0x20;
#endif
constexpr uint8_t MCP_SWITCH_ADDR = 0x21;

#if defined(KEYBOARD_DRIVER_MCP23017)
extern Adafruit_MCP23X17 keyboardExpander;
#endif
extern Adafruit_MCP23X17 switchExpander;

#if defined(KEYBOARD_DRIVER_TTP229)
constexpr uint8_t KEY_COUNT = TTP229_KEY_COUNT;
#else
constexpr uint8_t KEY_COLS = 5;
constexpr uint8_t KEY_ROWS = 5;
constexpr uint8_t KEY_COUNT = KEY_COLS * KEY_ROWS;
#endif
extern const uint8_t keyMidiNotes[KEY_COUNT];

enum ControlSwitch {
  SWITCH_RECORD = 0,
  SWITCH_PLAY = 1,
  SWITCH_CLEAR = 2,
  SWITCH_HOLD = 3,
  SWITCH_SYNC = 4,
  SWITCH_RANDOM = 5
};

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C display;

/**
 * @brief グローバルなシンセパラメータ構造体
 */
extern SynthParams params;
// ポリフォニーボイス数
constexpr uint8_t POLY_VOICES = 4;

// 各ボイスごとのオシレータ/フェーズ/エンベロープ/フィルタは静的確保されます。
// 各配列の実体は synth_state.cpp に定義されています。
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscSin[POLY_VOICES];
extern Oscil<TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri[POLY_VOICES];
extern Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw[POLY_VOICES];
extern Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSquare[POLY_VOICES];
extern Phasor<AUDIO_RATE> pulsePhasor[POLY_VOICES];

// グローバル LFO（共有）
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoPitch;
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoFilter;

// 各ボイスのエンベロープ/フィルタはポインタで扱う
extern ADSR<CONTROL_RATE, AUDIO_RATE> envelopeInstance[POLY_VOICES];
extern LowPassFilter filterInstance[POLY_VOICES];

// グローバルパラメータとボイス状態
extern float voiceCurrentFreq[POLY_VOICES];
extern float voiceTargetFreq[POLY_VOICES];
extern bool voiceActive[POLY_VOICES];
extern uint8_t voiceNote[POLY_VOICES];
