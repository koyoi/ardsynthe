#include "synth_state.h"

const uint8_t analogPins[ANALOG_INPUT_COUNT] = {A0, A1, A2, A3, A4, A5};

#if defined(KEYBOARD_DRIVER_TTP229)
const uint8_t keyMidiNotes[KEY_COUNT] = {
  48, 49, 50, 51,
  52, 53, 54, 55,
  56, 57, 58, 59,
  60, 61, 62, 63
};
#else
const uint8_t keyMidiNotes[KEY_COUNT] = {
  48, 49, 50, 51, 52,
  53, 54, 55, 56, 57,
  58, 59, 60, 61, 62,
  63, 64, 65, 66, 67,
  68, 69, 70, 71, 72
};
#endif

#if defined(KEYBOARD_DRIVER_MCP23017)
Adafruit_MCP23X17 keyboardExpander;
#endif
Adafruit_MCP23X17 switchExpander;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// ボイス配列の実体（静的確保：ヒープを使用しない）
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscSin[POLY_VOICES] = { {SIN2048_DATA}, {SIN2048_DATA}, {SIN2048_DATA}, {SIN2048_DATA} };
Oscil<TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri[POLY_VOICES] = { {TRIANGLE2048_DATA}, {TRIANGLE2048_DATA}, {TRIANGLE2048_DATA}, {TRIANGLE2048_DATA} };
Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw[POLY_VOICES] = { {SAW2048_DATA}, {SAW2048_DATA}, {SAW2048_DATA}, {SAW2048_DATA} };
Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSquare[POLY_VOICES] = { {SQUARE_NO_ALIAS_2048_DATA}, {SQUARE_NO_ALIAS_2048_DATA}, {SQUARE_NO_ALIAS_2048_DATA}, {SQUARE_NO_ALIAS_2048_DATA} };
Phasor<AUDIO_RATE> pulsePhasor[POLY_VOICES];

Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoPitch(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoFilter(SIN2048_DATA);

ADSR<CONTROL_RATE, AUDIO_RATE> envelopeInstance[POLY_VOICES];
LowPassFilter filterInstance[POLY_VOICES];

SynthParams params;
float voiceCurrentFreq[POLY_VOICES] = {440.0f, 440.0f, 440.0f, 440.0f};
float voiceTargetFreq[POLY_VOICES] = {440.0f, 440.0f, 440.0f, 440.0f};
bool voiceActive[POLY_VOICES] = {false, false, false, false};
uint8_t voiceNote[POLY_VOICES] = {0, 0, 0, 0};
