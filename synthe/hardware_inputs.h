#pragma once

#include <Arduino.h>

/**
 * @brief キーボード用I2Cエキスパンダを初期化する
 */
void setupKeyboardExpander();

/**
 * @brief キーボードをスキャンしてキーイベントを処理する
 *
 * 各列を順にアクティブにして行入力を読み、状態変化があればノートのオン/オフを発火します。
 */
void scanKeyboard();

/**
 * @brief スイッチ用 I2C エキスパンダを初期化する
 */
void setupSwitchExpander();

/**
 * @brief スイッチの状態を読み取り、エッジ検出により操作を実行する
 */
void readSwitches();

/**
 * @brief アナログ入力（ポット）を読み取り params 等に反映する
 */
void readAnalogs();
