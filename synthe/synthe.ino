#include <Mozzi.h>
#include <Oscil.h>
#include <ADSR.h>
#include <ResonantFilter.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <tables/triangle2048_int8.h>

#define CONTROL_RATE 64

// ---- オシレータ ----
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscSin(SIN2048_DATA);
Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw(SAW2048_DATA);
Oscil <SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSquare(SQUARE_NO_ALIAS_2048_DATA);
Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri(TRIANGLE2048_DATA);

// LFO
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> lfo(SIN2048_DATA);

// フィルタ
LowPassFilter lpf;

// エンベロープ
ADSR <CONTROL_RATE, AUDIO_RATE> env;

// 入力ピン
const int buttonPins[7] = {2, 3, 4, 5, 6, 7, 8}; // 7つに拡張
const int vrPins[6] = {A0, A1, A2, A3, A4, A5};

// 出力ピン
const int outPin1 = 9;
const int outPin2 = 10;

// パラメータ
int waveform = 0;
int mode = 0;
bool playing = false;
float freq = 440;
float amp = 128;
float lfoDepth = 0;
float lfoRate = 2.0;
float cutoff = 800;
float resonance = 0.7;
unsigned long sweepStart;

// エンベロープ
int attack = 50;
int decay = 200;
int sustain = 180;
int release = 400;

// ---- ボタン処理 ----
void readButtons() {
  for (int i = 0; i < 7; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      switch (i) {
        case 0: playing = !playing; if (playing) env.noteOn(); else env.noteOff(); break;
        case 1: mode = (mode + 1) % 4; break;
        case 2: waveform = (waveform + 1) % 4; break;
        case 3: env.noteOn(); break;
        case 4: env.noteOff(); break;
        case 5: cutoff = random(200, 2000); break;
        case 6: resonance = random(50, 200) / 100.0; break; // レゾナンス変更
      }
    }
  }
}

// ---- VR入力処理 ----
void readVR() {
  int v0 = analogRead(vrPins[0]); // 周波数
  int v1 = analogRead(vrPins[1]); // 振幅
  int v2 = analogRead(vrPins[2]); // LFO rate
  int v3 = analogRead(vrPins[3]); // LFO depth
  int v4 = analogRead(vrPins[4]); // cutoff
  int v5 = analogRead(vrPins[5]); // resonance

  freq = map(v0, 0, 1023, 50, 2000);
  amp  = map(v1, 0, 1023, 0, 255);
  lfoRate  = map(v2, 0, 1023, 0, 10);
  lfoDepth = map(v3, 0, 1023, 0, 200);
  cutoff   = map(v4, 0, 1023, 200, 3000);
  resonance= map(v5, 0, 1023, 50, 200) / 100.0;

  env.setAttack(attack);
  env.setDecay(decay);
  env.setSustain(sustain);
  env.setRelease(release);

}

// ---- 波形生成 ----
int getOscSample() {
  switch (waveform) {
    case 0: return oscSin.next();
    case 1: return oscSaw.next();
    case 2: return oscSquare.next();
    case 3: return oscTri.next();
  }
  return 0;
}

AudioOutput updateAudio() {
  if (!playing) return MonoOutput::from8Bit(128);

  // LFOを適用
  lfo.setFreq(lfoRate);
  int lfoVal = lfo.next();

  // ピッチにLFO
  float modulatedFreq = freq + (lfoVal * lfoDepth / 128.0);
  oscSin.setFreq(modulatedFreq);
  oscSaw.setFreq(modulatedFreq);
  oscSquare.setFreq(modulatedFreq);
  oscTri.setFreq(modulatedFreq);

  // 波形サンプル
  int rawSample = getOscSample();

  // エンベロープ適用
  int envVal = env.next();
  int sample = (rawSample * amp * envVal) >> 16;

  // フィルタにLFOをかける
  float cutoffMod = cutoff + (lfoVal * 10);
  lpf.setCutoffFreqAndResonance(cutoffMod, resonance);
  int filtered = lpf.next(sample);

  return MonoOutput::from8Bit(filtered + 128);
}

// ---- 制御処理 ----
void updateControl() {
  readButtons();
  readVR();
}

void setup() {
  for (int i = 0; i < 7; i++) pinMode(buttonPins[i], INPUT_PULLUP);
  startMozzi(CONTROL_RATE);
  oscSin.setFreq(freq);
  oscSaw.setFreq(freq);
  oscSquare.setFreq(freq);
  oscTri.setFreq(freq);
  lfo.setFreq(lfoRate);
  env.setAttack(attack);
  env.setDecay(decay);
  env.setSustain(sustain);
  env.setRelease(release);
  sweepStart = millis();
}

void loop() {
  audioHook();
}