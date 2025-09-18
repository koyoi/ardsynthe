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
  pushHeld(note);
  targetFreq = midiToFreq(static_cast<float>(note) + params.pitchOffset);
  envelope.noteOn();

  if (sequencerRecording && sequenceLength < MAX_SEQ_EVENTS) {
    sequenceBuffer[sequenceLength++] = {note, true, millis() - recordStartMs};
  }
}

void handleNoteOff(uint8_t note) {
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
  clearActiveSequencerNotes();
  sequenceLength = 0;
  sequenceDuration = 0;
  playbackIndex = 0;
  activeSequencerCount = 0;
}

void beginRecording() {
  sequencerPlaying = false;
  sequencerRecording = false;
  clearSequence();
  sequencerRecording = true;
  recordStartMs = millis();
}

void endRecording() {
  sequencerRecording = false;
  if (sequenceLength > 0) {
    finalizeSequence();
  }
}

void startPlayback() {
  if (sequenceLength == 0) {
    return;
  }
  sequencerPlaying = true;
  playbackIndex = 0;
  playbackStartMs = millis();
}

void stopPlayback() {
  sequencerPlaying = false;
  playbackIndex = 0;
}

void resetPlaybackMarkers() {
  playbackIndex = 0;
  playbackStartMs = millis();
  clearActiveSequencerNotes();
}

void updateSequencer() {
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
