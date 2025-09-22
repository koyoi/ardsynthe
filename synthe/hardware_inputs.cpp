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
  // キーボード用I2Cエキスパンダ初期化
  // 引数: なし
  // 説明: MCP23017 をキーボード行列用に設定し、行/列ピンの入出力を構成します。
  // 戻り値: なし
  // 副作用: エキスパンダのピンモードと状態を設定する。
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
  // キーボードスキャン
  // 引数: なし
  // 説明: 各列を順に LOW にして行の入力を読み取り、状態変化があれば
  //   `handleNoteOn` / `handleNoteOff` を呼び出します。
  // 戻り値: なし
  // 副作用: キーノートのオン/オフイベントを発生させる。
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
  // スイッチ用 I2C エキスパンダ初期化
  // 引数: なし
  // 説明: MCP23017 をスイッチ（タクトスイッチ）用に設定します。
  // 戻り値: なし
  // 副作用: エキスパンダのピンモードを INPUT_PULLUP に設定する。
  switchExpander.begin_I2C(MCP_SWITCH_ADDR);
  for (uint8_t i = 0; i < 8; ++i) {
    switchExpander.pinMode(i, INPUT_PULLUP);
  }
}

void readSwitches() {
  // スイッチ読み取りおよび押下イベント処理
  // 引数: なし
  // 説明: 各スイッチの状態を読み、押下エッジを検出したら対応する操作
  //   （録音、再生、クリア、ホールド、同期、ランダム）を実行します。
  // 戻り値: なし
  // 副作用: グローバルな sequencer 状態やノート状態を更新する。
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
  // アナログ入力読み取り
  // 引数: なし
  // 説明: 各ポットから正規化値を読み、パラメータ（モーフ、エンベロープ、フィルタ等）に反映します。
  // 戻り値: なし
  // 副作用: `params` と関連するエンベロープ/LFO の設定を更新する。
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
