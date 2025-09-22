#pragma once

/**
 * @brief MIDI 入力を処理する（Serial1 からの MIDI を想定）
 *
 * 受信した MIDI メッセージを解析し、ノートオン/オフ等の処理を行います。
 */
void handleMIDI();
