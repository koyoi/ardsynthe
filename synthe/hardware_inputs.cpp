#include "hardware_inputs.h"

#include "sequencer.h"
#include "synth_state.h"

#include <MozziGuts.h>
#include <string.h>

namespace {
uint8_t lastKeyState[KEY_COUNT];

float readNormalizedPot(uint8_t pin) {
  uint16_t value = mozziAnalogRead(pin);
  return static_cast<float>(value) / ANALOG_MAX_VALUE;
}
}

void setupKeyboardExpander() {
  keyboardExpander.begin_I2C(MCP_KEYBOARD_ADDR);

  for (uint8_t col = 0; col < KEY_COLS; ++col) {
    keyboardExpander.pinMode(col, OUTPUT);
    keyboardExpander.digitalWrite(col, HIGH);
  }
  for (uint8_t row = 0; row < KEY_ROWS; ++row) {
    keyboardExpander.pinMode(8 + row, INPUT_PULLUP);
  }

  memset(lastKeyState, 0, sizeof(lastKeyState));
}

void scanKeyboard() {
  for (uint8_t col = 0; col < KEY_COLS; ++col) {
    keyboardExpander.digitalWrite(col, LOW);
    delayMicroseconds(5);

    for (uint8_t row = 0; row < KEY_ROWS; ++row) {
      bool pressed = keyboardExpander.digitalRead(8 + row) == LOW;
      uint8_t index = row * KEY_COLS + col;
      if (pressed && !lastKeyState[index]) {
        handleNoteOn(keyMidiNotes[index]);
      } else if (!pressed && lastKeyState[index]) {
        handleNoteOff(keyMidiNotes[index]);
      }
      lastKeyState[index] = pressed;
    }
    keyboardExpander.digitalWrite(col, HIGH);
  }
}

void setupSwitchExpander() {
  switchExpander.begin_I2C(MCP_SWITCH_ADDR);
  for (uint8_t i = 0; i < 8; ++i) {
    switchExpander.pinMode(i, INPUT_PULLUP);
  }
}

void readSwitches() {
  bool recordPressed = !switchExpander.digitalRead(SWITCH_RECORD);
  bool playPressed = !switchExpander.digitalRead(SWITCH_PLAY);
  bool clearPressed = !switchExpander.digitalRead(SWITCH_CLEAR);
  bool holdPressed = !switchExpander.digitalRead(SWITCH_HOLD);
  bool syncPressed = !switchExpander.digitalRead(SWITCH_SYNC);
  bool randomPressed = !switchExpander.digitalRead(SWITCH_RANDOM);

  static bool lastRecord = false;
  static bool lastPlay = false;
  static bool lastClear = false;
  static bool lastHold = false;
  static bool lastSync = false;
  static bool lastRandom = false;

  if (recordPressed && !lastRecord) {
    if (isSequencerRecording()) {
      endRecording();
    } else {
      beginRecording();
    }
  }
  if (playPressed && !lastPlay) {
    if (isSequencerPlaying()) {
      stopPlayback();
      resetPlaybackMarkers();
    } else {
      resetPlaybackMarkers();
      startPlayback();
    }
  }
  if (clearPressed && !lastClear) {
    stopPlayback();
    abortRecording();
    clearSequence();
  }
  if (holdPressed && !lastHold) {
    releaseAllHeldNotes();
  }
  if (syncPressed && !lastSync) {
    resetPlaybackMarkers();
  }
  if (randomPressed && !lastRandom) {
    triggerRandomNote();
  }

  lastRecord = recordPressed;
  lastPlay = playPressed;
  lastClear = clearPressed;
  lastHold = holdPressed;
  lastSync = syncPressed;
  lastRandom = randomPressed;
}

void readAnalogs() {
  params.waveMorph = readNormalizedPot(analogPins[0]) * 4.0f;
  params.envAttack = 5.0f + 500.0f * readNormalizedPot(analogPins[1]);
  params.envSustain = readNormalizedPot(analogPins[2]);
  params.envRelease = 20.0f + 1000.0f * readNormalizedPot(analogPins[3]);
  params.filterCutoff = 200.0f + 3200.0f * readNormalizedPot(analogPins[4]);
  params.filterResonance = 0.1f + 0.85f * readNormalizedPot(analogPins[5]);

  envelope.setAttack(params.envAttack);
  envelope.setDecay(0);
  envelope.setSustain(static_cast<uint8_t>(params.envSustain * 255.0f));
  envelope.setRelease(params.envRelease);

  lfoPitch.setFreq(params.lfoRate);
  lfoFilter.setFreq(params.lfoRate * 0.75f);
}
