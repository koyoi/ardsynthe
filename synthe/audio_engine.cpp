#include "audio_engine.h"

#include "hardware_inputs.h"
#include "midi_input.h"
#include "sequencer.h"
#include "synth_state.h"
#include "visualizer.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr uint16_t CLICK_LENGTH = (AUDIO_RATE / 400) ? (AUDIO_RATE / 400) : 1;
volatile uint16_t clickSamplesRemaining = 0;

int16_t computeWaveSample() {
  // 波形選択・ブレンドを行い最終波形サンプルを計算する
  // 引数: なし
  // 説明: 4 種類のオシレーター（sin, tri, saw, square/pulse）から現在のモーフ位置に
  //   基づき2波形を線形補間して出力を決定します。
  // 戻り値: 16ビット波形サンプル（-32768..32767）
  // 副作用: なし（オシレーターの next() を呼ぶ副作用はオシレーター内部に限定される）
  // 既存の単一波形生成ロジックをボイスに適用するため、この関数は使わなくなりました。
  // 各ボイスごとに同様の処理を audio 更新内で行います。
  return 0;
}
}

void triggerClick() {
  // クリック音をトリガーする（UI の再生クリック用）
  // 引数: なし
  // 説明: クリック用のサンプル残数をセットし、次回のオーディオ更新でクリックを付加させます。
  // 戻り値: なし
  // 副作用: `clickSamplesRemaining` を変更する。
  clickSamplesRemaining = CLICK_LENGTH;
}

AudioOutput updateAudio() {
  // オーディオフレームの生成
  // 引数: なし
  // 説明: 現在の目標周波数へスムーズに追従させ、波形生成、エンベロープ、LFO、フィルタ処理を適用して
  //   最終的な 16bit サンプルを返します。また、FFT 用のサンプルをバッファへプッシュします。
  // 戻り値: AudioOutput（モノラル）
  // 副作用: グローバル状態（currentFreq, oscの位相, envelope, filter など）を進める。
  // ポリフォニック対応のオーディオ生成
  int32_t mix = 0;
  for (uint8_t v = 0; v < POLY_VOICES; ++v) {
    if (!voiceActive[v]) continue;

    float freqDiff = voiceTargetFreq[v] - voiceCurrentFreq[v];
    voiceCurrentFreq[v] += freqDiff * 0.02f;

  // 各ボイスのオシレータ周波数を設定
  oscSin[v].setFreq(voiceCurrentFreq[v]);
  oscTri[v].setFreq(voiceCurrentFreq[v]);
  oscSaw[v].setFreq(voiceCurrentFreq[v]);
  oscSquare[v].setFreq(voiceCurrentFreq[v]);
  pulsePhasor[v].setFreq(voiceCurrentFreq[v]);

  int16_t sinSample = oscSin[v].next();
  int16_t triSample = oscTri[v].next();
  int16_t sawSample = oscSaw[v].next();
  int16_t squareSample = oscSquare[v].next();

    float morph = constrain(params.waveMorph, 0.0f, 4.0f);
    int region = static_cast<int>(morph);
    float blend = morph - region;

    float pulseWidth = 0.5f;
    if (region == 2) {
      pulseWidth = 0.1f + 0.8f * blend;
    } else if (region == 3) {
      pulseWidth = 0.9f - 0.4f * blend;
    }

  uint16_t phase = pulsePhasor[v].next();
    int16_t pulseSample = (phase < static_cast<uint16_t>(pulseWidth * 65535.0f)) ? 127 : -128;

    auto selectWave = [&](int index) -> int16_t {
      switch (index) {
        case 0: return sinSample;
        case 1: return triSample;
        case 2: return sawSample;
        case 3: return pulseSample;
        default: return squareSample;
      }
    };

    int16_t first = selectWave(region);
    int16_t second = selectWave(min(region + 1, 4));
    int16_t baseSample = first + static_cast<int16_t>((second - first) * blend);

  int16_t envVal = envelopeInstance[v].next();
    int16_t amplitude = (baseSample * envVal) >> 8;

    int16_t lfoPitchOffset = (lfoPitch.next() * params.lfoDepthPitch) / 128.0f;
    float pitchFactor = powf(2.0f, lfoPitchOffset / 12.0f);
    float modulatedCutoff = params.filterCutoff + (lfoFilter.next() * params.lfoDepthFilter) / 128.0f;
    modulatedCutoff = constrain(modulatedCutoff, 40.0f, 5000.0f);

  filterInstance[v].setCutoffFreqAndResonance(modulatedCutoff * pitchFactor, params.filterResonance);
  int16_t filtered = filterInstance[v].next(amplitude);

    int16_t finalSample = static_cast<int16_t>(filtered * params.masterGain);

    mix += finalSample;
  }

  // ミキシング: クリッピングを防ぎつつ 16bit に収める
  mix = constrain(mix, -32767, 32767);
  int16_t outSample = static_cast<int16_t>(mix);

  if (clickSamplesRemaining > 0) {
    float env = static_cast<float>(clickSamplesRemaining) / static_cast<float>(CLICK_LENGTH);
    int16_t clickValue = static_cast<int16_t>(((clickSamplesRemaining & 1) ? 1 : -1) * env * 6000.0f);
    outSample = constrain(outSample + clickValue, -32767, 32767);
    clickSamplesRemaining--;
  }

  pushSampleForFFT(outSample);

  return MonoOutput::from16Bit(outSample);
}

void updateControl() {
  // コントロールレートごとの更新処理
  // 引数: なし
  // 説明: アナログ入力・キーボード・スイッチ・MIDI・シーケンサなどの入力を処理し、
  //   表示更新と FFT 計算を順次呼び出します。
  // 戻り値: なし
  // 副作用: 多数のグローバル状態を更新する（params, sequencer, display, FFT バッファ等）。
  readAnalogs();
  scanKeyboard();
  readSwitches();
  handleMIDI();
  updateSequencer();
  updateRandomTrigger();
  updateDisplay();
  computeFFT();
}
