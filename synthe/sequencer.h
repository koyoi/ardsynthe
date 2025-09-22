#pragma once

#include <Arduino.h>

/**
 * @brief ノートオンイベントの処理
 * @param note MIDIノート番号
 *
 * ノートを保持リストに追加し、ターゲット周波数を更新してエンベロープを開始します。
 */
void handleNoteOn(uint8_t note);

/**
 * @brief ノートオフイベントの処理
 * @param note MIDIノート番号
 *
 * ノートを保持リストから削除し、必要ならエンベロープをオフにします。
 */
void handleNoteOff(uint8_t note);

/**
 * @brief シーケンスを完全にクリアする
 *
 * シーケンスバッファと再生マーカー、アクティブノートをリセットします。
 */
void clearSequence();

/**
 * @brief 録音を開始する
 *
 * 現在のシーケンスをクリアし、録音モードに入ります。
 */
void beginRecording();

/**
 * @brief 録音を終了する
 *
 * 録音を停止し、録音済みシーケンスの最終化を行います。
 */
void endRecording();

/**
 * @brief シーケンス再生を開始する
 */
void startPlayback();

/**
 * @brief シーケンス再生を停止する
 */
void stopPlayback();

/**
 * @brief 再生位置とタイマーをリセットする
 */
void resetPlaybackMarkers();

/**
 * @brief シーケンサの定期更新（イベント発火）
 */
void updateSequencer();

/**
 * @brief ランダムトリガの更新（時間経過でのランダムノート解放）
 */
void updateRandomTrigger();

/**
 * @brief 録音中かどうかを返す
 */
bool isSequencerRecording();

/**
 * @brief 再生中かどうかを返す
 */
bool isSequencerPlaying();

/**
 * @brief 現在のシーケンス長を返す
 */
uint16_t getSequenceLength();

/**
 * @brief 録音を中止する（途中キャンセル）
 */
void abortRecording();

/**
 * @brief 保持中のノートをすべて解放する
 */
void releaseAllHeldNotes();

/**
 * @brief ランダムノートをトリガーする
 */
void triggerRandomNote();
