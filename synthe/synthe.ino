#include "config.h"

#include <Arduino.h>
#include <Mozzi.h>
#include <Wire.h>

#include "audio_engine.h"
#include "hardware_inputs.h"
#include "sequencer.h"
#include "synth_state.h"
#include "visualizer.h"

// ============================================================
//  Synth configuration for STM32F103 (Blue Pill) with Mozzi
// ============================================================
//  * Keyboard input can be switched at build time between an MCP23017 matrix
//    expander (25-key matrix) or a TTP229 capacitive keypad via `config.h`
//  * One MCP23017 I2C port expander is used for control switches (6 tactile switches)
//  * Six analog pots (VR) provide oscillator morphing, envelope (ASR),
//    filter cutoff/resonance, LFO rate/depth etc.
//  * Serial MIDI input on Serial1 (31250 bps)
//  * I2C OLED (SSD1306 etc.) driven with U8g2 for visual feedback of
//    waveform and FFT spectrum.
//  * Single voice, monophonic but legato aware, simple realtime recorder.
// ============================================================

void setup() {
  // 初期セットアップ
  // 説明: I2C の初期化、ディスプレイ初期化、ランダムシード設定、
  //   エキスパンダと MIDI シリアル、エンベロープ/LFO の初期値設定を行います。
  // 戻り値: なし
  // 副作用: ハードウェア初期化を行う。
  Wire.begin();
  display.begin();
  display.clearBuffer();
  display.sendBuffer();

  randomSeed(analogRead(analogPins[0]));

  setupKeyboardExpander();
  setupSwitchExpander();


  Serial1.begin(31250);

  // 各ボイスのエンベロープ設定を初期化
  for (uint8_t i = 0; i < POLY_VOICES; ++i) {
    envelopeInstance[i].setAttackTime(static_cast<unsigned int>(params.envAttack));
    envelopeInstance[i].setDecayTime(0);
    envelopeInstance[i].setSustainLevel(static_cast<uint8_t>(params.envSustain * 255.0f));
    envelopeInstance[i].setReleaseTime(static_cast<unsigned int>(params.envRelease));
  }

  lfoPitch.setFreq(params.lfoRate);
  lfoFilter.setFreq(params.lfoRate * 0.75f);

  startMozzi(MOZZI_CONTROL_RATE);
}

void loop() {
  // メインループ
  // 説明: Mozzi ライブラリのオーディオフックを呼び出し、オーディオ・コントロールの更新を行います。
  // 戻り値: なし
  // 副作用: オーディオ出力とコントロール更新が実行される。
  audioHook();
}
