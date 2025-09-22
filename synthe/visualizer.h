#pragma once

#include <Arduino.h>

/**
 * @brief ディスプレイを更新する
 *
 * 周波数やパラメータ（モーフ、フィルタ、エンベロープ、シーケンサ状態）を表示し、
 * 波形（左）とスペクトラム（右）を描画してバッファを送信します。
 *
 * @note 更新間隔は内部で制限されています（約50ms）。
 */
void updateDisplay();

/**
 * @brief FFT 用の波形サンプルを追加する
 *
 * @param sample 16bit PCM 相当のサンプル値（-32768..32767）
 *
 * @details 呼び出されるとサンプルは環状バッファに格納され、後続の FFT 計算で使用されます。
 * @note この関数は割り込みやオーディオコールバックから呼ばれる可能性があるため、軽量であるべきです。
 */
void pushSampleForFFT(int16_t sample);

/**
 * @brief FFT を計算してスペクトル結果を更新する
 *
 * @details 内部の波形バッファから FFT_SAMPLES 分のデータを取り出し、窓関数適用、FFT 計算、
 *          複素->振幅変換を実行します。結果はグローバル配列（fftReal/fftImag）に格納されます。
 * @note 実行間隔は内部で制限されています（約100ms）。
 */
void computeFFT();
