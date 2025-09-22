#include "sequencer.h"

#include "audio_engine.h"
#include "synth_state.h"

#include <Arduino.h>
#include <math.h>

namespace {
struct SequencerEvent {
  uint8_t note;
  bool noteOn;
  uint32_t timestamp;
};

constexpr uint16_t MAX_SEQ_EVENTS = 256;
SequencerEvent sequenceBuffer[MAX_SEQ_EVENTS];
uint16_t sequenceLength = 0;
uint32_t sequenceDuration = 0;

bool sequencerRecording = false;
bool sequencerPlaying = false;

uint32_t recordStartMs = 0;
uint32_t playbackStartMs = 0;
uint16_t playbackIndex = 0;

uint32_t sequenceOriginalTimestamp[MAX_SEQ_EVENTS];

uint8_t heldNotes[KEY_COUNT];
uint8_t heldCount = 0;

uint8_t activeSequencerNotes[KEY_COUNT];
uint8_t activeSequencerCount = 0;

bool randomNoteActive = false;
uint8_t randomNoteValue = 0;
uint32_t randomNoteStart = 0;

float midiToFreq(float note) {
  return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void pushHeld(uint8_t note) {
  if (heldCount < KEY_COUNT) {
    heldNotes[heldCount++] = note;
  }
}

void popHeld(uint8_t note) {
  for (uint8_t i = 0; i < heldCount; ++i) {
    if (heldNotes[i] == note) {
      for (uint8_t j = i; j < heldCount - 1; ++j) {
        heldNotes[j] = heldNotes[j + 1];
      }
      heldCount--;
      break;
    }
  }
}

uint8_t currentHeldNote() {
  return heldCount == 0 ? 0 : heldNotes[heldCount - 1];
}

void registerSequencerNote(uint8_t note) {
  if (activeSequencerCount < KEY_COUNT) {
    activeSequencerNotes[activeSequencerCount++] = note;
  }
}

void unregisterSequencerNote(uint8_t note) {
  for (uint8_t i = 0; i < activeSequencerCount; ++i) {
    if (activeSequencerNotes[i] == note) {
      for (uint8_t j = i; j < activeSequencerCount - 1; ++j) {
        activeSequencerNotes[j] = activeSequencerNotes[j + 1];
      }
      activeSequencerCount--;
      break;
    }
  }
}

void clearActiveSequencerNotes() {
  while (activeSequencerCount > 0) {
    uint8_t note = activeSequencerNotes[activeSequencerCount - 1];
    handleNoteOff(note);
    activeSequencerCount--;
  }
}

void finalizeSequence() {
  if (sequenceLength == 0) {
    sequenceDuration = 0;
    return;
  }
  for (uint16_t i = 0; i < sequenceLength; ++i) {
    sequenceOriginalTimestamp[i] = sequenceBuffer[i].timestamp;
  }
  sequenceDuration = sequenceOriginalTimestamp[sequenceLength - 1] + 1;
}
}  // namespace

void handleNoteOn(uint8_t note) {
  // ノートオン処理
  // 引数:
  //   note: MIDIノート番号
  // 説明:
  //   キー保持リストにノートを追加し、ターゲット周波数を更新してエンベロープを開始します。
  //   シーケンサが録音中であればイベントバッファに記録します。
  // 戻り値: なし
  // 副作用: グローバルな synth 状態（targetFreq, envelope, sequenceBuffer 等）を更新する。
  pushHeld(note);
  targetFreq = midiToFreq(static_cast<float>(note) + params.pitchOffset);
  envelope.noteOn();

  if (sequencerRecording && sequenceLength < MAX_SEQ_EVENTS) {
    sequenceBuffer[sequenceLength++] = {note, true, millis() - recordStartMs};
  }
}

void handleNoteOff(uint8_t note) {
  // ノートオフ処理
  // 引数:
  //   note: MIDIノート番号
  // 説明:
  //   保持リストからノートを削除し、必要に応じてエンベロープをオフにします。
  //   録音中であればノートオフイベントを記録します。
  // 戻り値: なし
  // 副作用: グローバルな synth 状態を更新する。
  popHeld(note);
  if (sequencerRecording && sequenceLength < MAX_SEQ_EVENTS) {
    sequenceBuffer[sequenceLength++] = {note, false, millis() - recordStartMs};
  }

  if (heldCount == 0) {
    envelope.noteOff();
  } else {
    targetFreq = midiToFreq(static_cast<float>(currentHeldNote()) + params.pitchOffset);
  }
}

void clearSequence() {
  // シーケンスをクリアする
  // 引数: なし
  // 説明: 再生中のノートをすべてオフにして、シーケンスバッファと再生マーカーをリセットします。
  // 戻り値: なし
  // 副作用: sequenceLength, sequenceDuration, playbackIndex などをリセットする。
  clearActiveSequencerNotes();
  sequenceLength = 0;
  sequenceDuration = 0;
  playbackIndex = 0;
  activeSequencerCount = 0;
}

void beginRecording() {
  // 録音開始
  // 引数: なし
  // 説明: 現在の再生を停止してシーケンスをクリアし、録音モードに入ります。
  // 戻り値: なし
  // 副作用: sequencerRecording を true にして記録開始時刻を保存する。
  sequencerPlaying = false;
  sequencerRecording = false;
  clearSequence();
  sequencerRecording = true;
  recordStartMs = millis();
}

void endRecording() {
  // 録音終了
  // 引数: なし
  // 説明: 録音モードを終了し、録音されたシーケンスがあれば最終化処理を行います。
  // 戻り値: なし
  // 副作用: sequencerRecording を false にする。
  sequencerRecording = false;
  if (sequenceLength > 0) {
    finalizeSequence();
  }
}

void startPlayback() {
  // 再生開始
  // 引数: なし
  // 説明: シーケンスが存在する場合、再生モードに入り再生マーカーを初期化します。
  // 戻り値: なし
  // 副作用: sequencerPlaying を true にする。
  if (sequenceLength == 0) {
    return;
  }
  sequencerPlaying = true;
  playbackIndex = 0;
  playbackStartMs = millis();
}

void stopPlayback() {
  // 再生停止
  // 引数: なし
  // 説明: 再生モードを終了し、再生インデックスをリセットします。
  // 戻り値: なし
  // 副作用: sequencerPlaying を false にする。
  sequencerPlaying = false;
  playbackIndex = 0;
}

void resetPlaybackMarkers() {
  // 再生マーカーをリセット
  // 引数: なし
  // 説明: 再生位置と開始時刻を現在に合わせ、再生中のノートをクリアします。
  // 戻り値: なし
  // 副作用: playbackIndex と playbackStartMs を更新する。
  playbackIndex = 0;
  playbackStartMs = millis();
  clearActiveSequencerNotes();
}

void updateSequencer() {
  // シーケンサの定期更新
  // 引数: なし
  // 説明: 再生中であれば経過時間に基づいてシーケンスイベントを発火します。
  // 戻り値: なし
  // 副作用: ノートのオン/オフを実行し、再生位置を進める。
  if (!sequencerPlaying || sequenceLength == 0) {
    return;
  }

  uint32_t now = millis();
  uint32_t elapsed = now - playbackStartMs;

  while (playbackIndex < sequenceLength && elapsed >= sequenceOriginalTimestamp[playbackIndex]) {
    SequencerEvent &evt = sequenceBuffer[playbackIndex];
    if (evt.noteOn) {
      handleNoteOn(evt.note);
      registerSequencerNote(evt.note);
      triggerClick();
    } else {
      handleNoteOff(evt.note);
      unregisterSequencerNote(evt.note);
    }
    playbackIndex++;
  }

  if (sequenceDuration > 0 && elapsed >= sequenceDuration) {
    playbackStartMs = now;
    playbackIndex = 0;
    clearActiveSequencerNotes();
  }
}

void updateRandomTrigger() {
  if (randomNoteActive && millis() - randomNoteStart > 200) {
    handleNoteOff(randomNoteValue);
    randomNoteActive = false;
  }
}

bool isSequencerRecording() {
  return sequencerRecording;
}

bool isSequencerPlaying() {
  return sequencerPlaying;
}

uint16_t getSequenceLength() {
  return sequenceLength;
}

void abortRecording() {
  sequencerRecording = false;
}

void releaseAllHeldNotes() {
  if (heldCount > 0) {
    envelope.noteOff();
    heldCount = 0;
  }
}

void triggerRandomNote() {
  if (randomNoteActive) {
    handleNoteOff(randomNoteValue);
    randomNoteActive = false;
  }
  randomNoteValue = random(48, 73);
  handleNoteOn(randomNoteValue);
  triggerClick();
  randomNoteStart = millis();
  randomNoteActive = true;
}
