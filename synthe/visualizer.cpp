#include "visualizer.h"

#include "sequencer.h"
#include "synth_state.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include <math.h>

namespace {
constexpr uint16_t FFT_SAMPLES = 128;
constexpr float FFT_SAMPLE_RATE = AUDIO_RATE;

double fftReal[FFT_SAMPLES];
double fftImag[FFT_SAMPLES];
arduinoFFT FFT(fftReal, fftImag, FFT_SAMPLES, FFT_SAMPLE_RATE);

int16_t waveformBuffer[FFT_SAMPLES];
volatile uint16_t waveformWriteIndex = 0;

void renderWaveform(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  // 描画用波形レンダラー
  // 引数:
  //   x, y: フレームの左上座標（ピクセル）
  //   width, height: フレームの幅と高さ（ピクセル）
  // 説明:
  //   内部バッファ `waveformBuffer` から波形サンプルを読み取り、指定領域内に波形をプロットします。
  //   バッファは環状で書き込みインデックス `waveformWriteIndex` により最新位置が管理されています。
  // 戻り値: なし
  // 副作用: ディスプレイにピクセルを描画する（`display` に依存）。
  // 注意: width が大きい場合はサンプルを間引いて表示します（パフォーマンスと表示密度の両立）。
  display.drawFrame(x, y, width, height);
  uint8_t step = max<uint8_t>(1, FFT_SAMPLES / width);
  uint16_t baseIndex = waveformWriteIndex;
  for (uint8_t i = 0; i < width - 1; ++i) {
    uint16_t index = (baseIndex + i * step) % FFT_SAMPLES;
    int16_t sample = waveformBuffer[index];
    int centered = static_cast<int>(y + (height / 2) - (sample / 32768.0f) * (height / 2 - 1));
    centered = constrain(centered, y + 1, y + height - 2);
    display.drawPixel(x + i + 1, centered);
  }
}

void renderSpectrum(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  // スペクトラム（周波数成分）レンダラー
  // 引数:
  //   x, y: フレームの左上座標（ピクセル）
  //   width, height: フレームの幅と高さ（ピクセル）
  // 説明:
  //   FFT の計算結果（`fftReal` に格納された振幅）を縦棒グラフとして描画します。
  //   対数スケール（log10(1 + magnitude)）で高さを決め、表示領域に収まるよう制限します。
  // 戻り値: なし
  // 副作用: ディスプレイにラインを描画する。
  // 注意: FFT は別関数 `computeFFT()` により更新されるため、本関数は描画のみを担当します。
  display.drawFrame(x, y, width, height);
  uint8_t bins = min<uint8_t>(width - 2, FFT_SAMPLES / 2);
  for (uint8_t i = 0; i < bins; ++i) {
    double magnitude = fftReal[i];
    int barHeight = static_cast<int>(log10(1 + magnitude) * (height - 2));
    barHeight = constrain(barHeight, 0, height - 2);
    display.drawLine(x + 1 + i, y + height - 1, x + 1 + i, y + height - 1 - barHeight);
  }
}
}

void updateDisplay() {
  // メインディスプレイ更新関数
  // 引数: なし
  // 説明:
  //   各種パラメータ（周波数、モーフ、フィルタ、エンベロープ、シーケンサ状態）を表示し、
  //   波形とスペクトラムの描画を行った後、バッファをディスプレイへ送信します。
  //   更新レートは約50ms 毎に制限されています（フレームレート抑制）。
  // 戻り値: なし
  // 副作用: 表示バッファのクリア・描画・送信を行う。
  // 注意: この関数は UI 更新を行うのみで、FFT の計算や波形サンプルの収集は別関数で行われます。
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 50) {
    return;
  }
  lastUpdate = now;

  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(0, 8);
  display.print("Freq:");
  display.print(static_cast<int>(currentFreq));
  display.print("Hz");

  display.setCursor(0, 16);
  display.print("Morph:");
  display.print(params.waveMorph, 2);

  display.setCursor(0, 24);
  display.print("Cut:");
  display.print(static_cast<int>(params.filterCutoff));
  display.print(" Res:");
  display.print(params.filterResonance, 2);

  display.setCursor(0, 32);
  display.print("ASR:");
  display.print(params.envAttack, 0);
  display.print("/");
  display.print(params.envSustain, 2);
  display.print("/");
  display.print(params.envRelease, 0);

  display.setCursor(0, 40);
  display.print("Seq: ");
  if (isSequencerRecording()) {
    display.print("REC");
  } else if (isSequencerPlaying()) {
    display.print("PLAY");
  } else {
    display.print("STOP");
  }
  display.print(" E:");
  display.print(getSequenceLength());

  renderWaveform(64, 0, 63, 31);
  renderSpectrum(64, 32, 63, 31);

  display.sendBuffer();
}

void pushSampleForFFT(int16_t sample) {
  // 波形サンプルを FFT 用環状バッファに追加する
  // 引数:
  //   sample: 16ビットPCM相当の波形サンプル（-32768..32767）
  // 説明:
  //   呼び出されると最新のサンプルが `waveformBuffer` に格納され、書き込みインデックスが進みます。
  // 戻り値: なし
  // 副作用: グローバルバッファを書き換える。割り込みコンテキストから呼ぶ可能性がある場合は
  //   `waveformWriteIndex` が volatile として定義されているため原子性に注意。
  waveformBuffer[waveformWriteIndex] = sample;
  waveformWriteIndex = (waveformWriteIndex + 1) % FFT_SAMPLES;
}

void computeFFT() {
  // FFT を計算して振幅スペクトルを更新する
  // 引数: なし
  // 説明:
  //   内部の波形環状バッファから一定数（FFT_SAMPLES）分のサンプルを取り出し、
  //   arduinoFFT ライブラリで窓関数・FFT・複素数からの振幅変換を行い、結果を `fftReal` に格納します。
  //   実行間隔は約100ms に制限されています。
  // 戻り値: なし
  // 副作用: `fftReal` / `fftImag` を上書きする。
  // 注意: 呼び出し元が定期的に呼ぶことを想定。データ取り出し時に `waveformWriteIndex` が更新される
  //   可能性があるため、正確な位相/時間整合が必要ならロックを検討してください。
  static uint32_t lastFFT = 0;
  uint32_t now = millis();
  if (now - lastFFT < 100) {
    return;
  }
  lastFFT = now;

  uint16_t start = waveformWriteIndex;
  for (uint16_t i = 0; i < FFT_SAMPLES; ++i) {
    uint16_t idx = (start + i) % FFT_SAMPLES;
    fftReal[i] = static_cast<double>(waveformBuffer[idx]);
    fftImag[i] = 0.0;
  }

  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();
}
