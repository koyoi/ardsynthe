#include "audio_engine.h"

#include "hardware_inputs.h"
#include "midi_input.h"
#include "sequencer.h"
#include "synth_state.h"
#include "visualizer.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr uint16_t CLICK_LENGTH = (AUDIO_RATE / 400) ? (AUDIO_RATE / 400) : 1;
volatile uint16_t clickSamplesRemaining = 0;

int16_t computeWaveSample() {
  int16_t sinSample = oscSin.next();
  int16_t triSample = oscTri.next();
  int16_t sawSample = oscSaw.next();
  int16_t squareSample = oscSquare.next();

  float morph = constrain(params.waveMorph, 0.0f, 4.0f);
  int region = static_cast<int>(morph);
  float blend = morph - region;

  float pulseWidth = 0.5f;
  if (region == 2) {
    pulseWidth = 0.1f + 0.8f * blend;
  } else if (region == 3) {
    pulseWidth = 0.9f - 0.4f * blend;
  }

  uint16_t phase = pulsePhasor.next();
  int16_t pulseSample = (phase < static_cast<uint16_t>(pulseWidth * 65535.0f)) ? 127 : -128;

  auto selectWave = [&](int index) -> int16_t {
    switch (index) {
      case 0: return sinSample;
      case 1: return triSample;
      case 2: return sawSample;
      case 3: return pulseSample;
      default: return squareSample;
    }
  };

  int16_t first = selectWave(region);
  int16_t second = selectWave(min(region + 1, 4));
  return first + static_cast<int16_t>((second - first) * blend);
}
}

void triggerClick() {
  clickSamplesRemaining = CLICK_LENGTH;
}

AudioOutput updateAudio() {
  float freqDiff = targetFreq - currentFreq;
  currentFreq += freqDiff * 0.02f;

  oscSin.setFreq(currentFreq);
  oscTri.setFreq(currentFreq);
  oscSaw.setFreq(currentFreq);
  oscSquare.setFreq(currentFreq);
  pulsePhasor.setFreq(currentFreq);

  int16_t baseSample = computeWaveSample();

  int16_t envVal = envelope.next();
  int16_t amplitude = (baseSample * envVal) >> 8;

  int16_t lfoPitchOffset = (lfoPitch.next() * params.lfoDepthPitch) / 128.0f;
  float pitchFactor = powf(2.0f, lfoPitchOffset / 12.0f);
  float modulatedCutoff = params.filterCutoff + (lfoFilter.next() * params.lfoDepthFilter) / 128.0f;
  modulatedCutoff = constrain(modulatedCutoff, 40.0f, 5000.0f);

  filter.setCutoffFreqAndResonance(modulatedCutoff * pitchFactor, params.filterResonance);
  int16_t filtered = filter.next(amplitude);

  int16_t finalSample = static_cast<int16_t>(filtered * params.masterGain);

  if (clickSamplesRemaining > 0) {
    float env = static_cast<float>(clickSamplesRemaining) / static_cast<float>(CLICK_LENGTH);
    int16_t clickValue = static_cast<int16_t>(((clickSamplesRemaining & 1) ? 1 : -1) * env * 6000.0f);
    finalSample = constrain(finalSample + clickValue, -32767, 32767);
    clickSamplesRemaining--;
  }

  pushSampleForFFT(finalSample);

  return MonoOutput::from16Bit(finalSample);
}

void updateControl() {
  readAnalogs();
  scanKeyboard();
  readSwitches();
  handleMIDI();
  updateSequencer();
  updateRandomTrigger();
  updateDisplay();
  computeFFT();
}
