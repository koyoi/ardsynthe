#pragma once

#include <Arduino.h>

void updateDisplay();
void pushSampleForFFT(int16_t sample);
void computeFFT();
