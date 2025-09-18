#pragma once

#include <Arduino.h>

void handleNoteOn(uint8_t note);
void handleNoteOff(uint8_t note);

void clearSequence();
void beginRecording();
void endRecording();
void startPlayback();
void stopPlayback();
void resetPlaybackMarkers();

void updateSequencer();
void updateRandomTrigger();

bool isSequencerRecording();
bool isSequencerPlaying();
uint16_t getSequenceLength();

void abortRecording();
void releaseAllHeldNotes();
void triggerRandomNote();
