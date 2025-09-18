#include "visualizer.h"

#include "sequencer.h"
#include "synth_state.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include <math.h>

namespace {
constexpr uint16_t FFT_SAMPLES = 128;
constexpr float FFT_SAMPLE_RATE = AUDIO_RATE;

double fftReal[FFT_SAMPLES];
double fftImag[FFT_SAMPLES];
arduinoFFT FFT(fftReal, fftImag, FFT_SAMPLES, FFT_SAMPLE_RATE);

int16_t waveformBuffer[FFT_SAMPLES];
volatile uint16_t waveformWriteIndex = 0;

void renderWaveform(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  display.drawFrame(x, y, width, height);
  uint8_t step = max<uint8_t>(1, FFT_SAMPLES / width);
  uint16_t baseIndex = waveformWriteIndex;
  for (uint8_t i = 0; i < width - 1; ++i) {
    uint16_t index = (baseIndex + i * step) % FFT_SAMPLES;
    int16_t sample = waveformBuffer[index];
    int centered = static_cast<int>(y + (height / 2) - (sample / 32768.0f) * (height / 2 - 1));
    centered = constrain(centered, y + 1, y + height - 2);
    display.drawPixel(x + i + 1, centered);
  }
}

void renderSpectrum(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
  display.drawFrame(x, y, width, height);
  uint8_t bins = min<uint8_t>(width - 2, FFT_SAMPLES / 2);
  for (uint8_t i = 0; i < bins; ++i) {
    double magnitude = fftReal[i];
    int barHeight = static_cast<int>(log10(1 + magnitude) * (height - 2));
    barHeight = constrain(barHeight, 0, height - 2);
    display.drawLine(x + 1 + i, y + height - 1, x + 1 + i, y + height - 1 - barHeight);
  }
}
}

void updateDisplay() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 50) {
    return;
  }
  lastUpdate = now;

  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(0, 8);
  display.print("Freq:");
  display.print(static_cast<int>(currentFreq));
  display.print("Hz");

  display.setCursor(0, 16);
  display.print("Morph:");
  display.print(params.waveMorph, 2);

  display.setCursor(0, 24);
  display.print("Cut:");
  display.print(static_cast<int>(params.filterCutoff));
  display.print(" Res:");
  display.print(params.filterResonance, 2);

  display.setCursor(0, 32);
  display.print("ASR:");
  display.print(params.envAttack, 0);
  display.print("/");
  display.print(params.envSustain, 2);
  display.print("/");
  display.print(params.envRelease, 0);

  display.setCursor(0, 40);
  display.print("Seq: ");
  if (isSequencerRecording()) {
    display.print("REC");
  } else if (isSequencerPlaying()) {
    display.print("PLAY");
  } else {
    display.print("STOP");
  }
  display.print(" E:");
  display.print(getSequenceLength());

  renderWaveform(64, 0, 63, 31);
  renderSpectrum(64, 32, 63, 31);

  display.sendBuffer();
}

void pushSampleForFFT(int16_t sample) {
  waveformBuffer[waveformWriteIndex] = sample;
  waveformWriteIndex = (waveformWriteIndex + 1) % FFT_SAMPLES;
}

void computeFFT() {
  static uint32_t lastFFT = 0;
  uint32_t now = millis();
  if (now - lastFFT < 100) {
    return;
  }
  lastFFT = now;

  uint16_t start = waveformWriteIndex;
  for (uint16_t i = 0; i < FFT_SAMPLES; ++i) {
    uint16_t idx = (start + i) % FFT_SAMPLES;
    fftReal[i] = static_cast<double>(waveformBuffer[idx]);
    fftImag[i] = 0.0;
  }

  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();
}
