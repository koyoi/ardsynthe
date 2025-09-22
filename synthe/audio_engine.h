#pragma once

#include "config.h"

#include <MozziGuts.h>

/**
 * @brief 次のオーディオフレームを生成して返す（Mozzi 用）
 *
 * 波形生成、エンベロープ、LFO、フィルタを適用し、FFT バッファへサンプルを追加します。
 * @return AudioOutput 生成されたオーディオ出力（モノラル）
 */
AudioOutput updateAudio();

/**
 * @brief コントロールレートでの更新（センサ/スイッチ/MIDI/表示/FFT呼び出し）
 */
void updateControl();

/**
 * @brief 再生クリックをトリガーする（UIフィードバック）
 */
void triggerClick();
