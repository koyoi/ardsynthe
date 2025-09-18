#include "config.h"

#include <Arduino.h>
#include <MozziGuts.h>
#include <Wire.h>

#include "audio_engine.h"
#include "hardware_inputs.h"
#include "sequencer.h"
#include "synth_state.h"
#include "visualizer.h"

// ============================================================
//  Synth configuration for STM32F103 (Blue Pill) with Mozzi
// ============================================================
//  * Two MCP23017 I2C port expanders are used
//      - Keyboard matrix (up to 25 keys for a little more than 2 octaves)
//      - Control switches (6 tactile switches)
//  * Six analog pots (VR) provide oscillator morphing, envelope (ASR),
//    filter cutoff/resonance, LFO rate/depth etc.
//  * Serial MIDI input on Serial1 (31250 bps)
//  * I2C OLED (SSD1306 etc.) driven with U8g2 for visual feedback of
//    waveform and FFT spectrum.
//  * Single voice, monophonic but legato aware, simple realtime recorder.
// ============================================================

void setup() {
  Wire.begin();
  display.begin();
  display.clearBuffer();
  display.sendBuffer();

  randomSeed(analogRead(analogPins[0]));

  setupKeyboardExpander();
  setupSwitchExpander();

  Serial1.begin(31250);

  envelope.setAttack(params.envAttack);
  envelope.setDecay(0);
  envelope.setSustain(static_cast<uint8_t>(params.envSustain * 255.0f));
  envelope.setRelease(params.envRelease);

  lfoPitch.setFreq(params.lfoRate);
  lfoFilter.setFreq(params.lfoRate * 0.75f);

  startMozzi(CONTROL_RATE);
}

void loop() {
  audioHook();
}
