#include <Arduino.h>
#include <Wire.h>
#include <Mozzi.h>
#include <Oscil.h>
#include <ADSR.h>
#include <LowPassFilter.h>
#include <tables/sin2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <Phasor.h>
#include <arduinoFFT.h>
#include <Adafruit_MCP23X17.h>
#include <U8g2lib.h>
#include <string.h>
#include <math.h>

// ============================================================
//  Synth configuration for STM32F103 (Blue Pill) with Mozzi
// ============================================================
//  * Two MCP23017 I2C port expanders are used
//      - Keyboard matrix (up to 25 keys for a little more than 2 octaves)
//      - Control switches (6 tactile switches)
//  * Six analog pots (VR) provide oscillator morphing, envelope (ASR),
//    filter cutoff/resonance, LFO rate/depth etc.
//  * Serial MIDI input on Serial1 (31250 bps)
//  * I2C OLED (SSD1306 etc.) driven with U8g2 for visual feedback of
//    waveform and FFT spectrum.
//  * Single voice, monophonic but legato aware, simple realtime recorder.
// ============================================================

#define AUDIO_MODE STANDARD_PLUS
#define CONTROL_RATE 128

// --------------------------- hardware -----------------------
static const uint8_t ANALOG_INPUTS = 6;
static const uint8_t analogPins[ANALOG_INPUTS] = {A0, A1, A2, A3, A4, A5};

#if defined(ARDUINO_ARCH_STM32)
static const float ANALOG_MAX_VALUE = 4095.0f;
#else
static const float ANALOG_MAX_VALUE = 1023.0f;
#endif

// MCP23017 addresses
static const uint8_t MCP_KEYBOARD_ADDR = 0x20;
static const uint8_t MCP_SWITCH_ADDR   = 0x21;

Adafruit_MCP23X17 keyboardExpander;
Adafruit_MCP23X17 switchExpander;

// Keyboard matrix layout (5 columns x 5 rows = 25 keys)
static const uint8_t KEY_COLS = 5;
static const uint8_t KEY_ROWS = 5;
static const uint8_t KEY_COUNT = KEY_COLS * KEY_ROWS;

// MIDI note numbers for a 2-octave+ keyboard (starting at C3)
static const uint8_t keyMidiNotes[KEY_COUNT] = {
  48, 49, 50, 51, 52,
  53, 54, 55, 56, 57,
  58, 59, 60, 61, 62,
  63, 64, 65, 66, 67,
  68, 69, 70, 71, 72
};

// Control switch bits on the second expander
enum ControlSwitch {
  SWITCH_RECORD = 0,
  SWITCH_PLAY   = 1,
  SWITCH_CLEAR  = 2,
  SWITCH_HOLD   = 3,
  SWITCH_SYNC   = 4,
  SWITCH_RANDOM = 5
};

// OLED display (I2C, 128x64 recommended)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// --------------------------- audio -------------------------
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscSin(SIN2048_DATA);
Oscil<TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri(TRIANGLE2048_DATA);
Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw(SAW2048_DATA);
Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSquare(SQUARE_NO_ALIAS_2048_DATA);

Phasor<AUDIO_RATE> pulsePhasor;

Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoPitch(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> lfoFilter(SIN2048_DATA);

ADSR<CONTROL_RATE, AUDIO_RATE> envelope;
LowPassFilter filter;

// Sequencer click (simple decaying burst)
static const uint16_t CLICK_LENGTH = (AUDIO_RATE / 400) ? (AUDIO_RATE / 400) : 1;
volatile uint16_t clickSamplesRemaining = 0;

// FFT for display
static const uint16_t FFT_SAMPLES = 128;  // power of two (Mozzi audio buffer friendly)
static const float FFT_SAMPLE_RATE = AUDIO_RATE;
double fftReal[FFT_SAMPLES];
double fftImag[FFT_SAMPLES];
arduinoFFT FFT = arduinoFFT(fftReal, fftImag, FFT_SAMPLES, FFT_SAMPLE_RATE);
int16_t waveformBuffer[FFT_SAMPLES];
volatile uint16_t waveformWriteIndex = 0;

// --------------------------- state -------------------------
struct SynthParams {
  float pitchOffset = 0.0f;      // +/- semitone from front panel
  float waveMorph = 0.0f;        // 0..4 => sine -> triangle -> saw -> pulse -> square
  float envAttack = 25.0f;       // ms
  float envSustain = 0.8f;       // 0..1
  float envRelease = 300.0f;     // ms
  float lfoRate = 4.0f;          // Hz
  float lfoDepthPitch = 0.3f;    // semitones
  float lfoDepthFilter = 200.0f; // Hz
  float filterCutoff = 1200.0f;  // Hz
  float filterResonance = 0.7f;  // 0.1..0.95
  float masterGain = 0.7f;       // 0..1
};

struct SequencerEvent {
  uint8_t note;
  bool noteOn;
  uint32_t timestamp;  // milliseconds from record start
};

static const uint16_t MAX_SEQ_EVENTS = 256;
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
static const uint8_t MAX_ACTIVE_SEQ_NOTES = KEY_COUNT;
uint8_t activeSequencerNotes[MAX_ACTIVE_SEQ_NOTES];
uint8_t activeSequencerCount = 0;
bool randomNoteActive = false;
uint8_t randomNoteValue = 0;
uint32_t randomNoteStart = 0;

SynthParams params;
float currentFreq = 440.0f;
float targetFreq = 440.0f;

uint8_t lastKeyState[KEY_COUNT];

// ------------------- helper functions ---------------------
float midiToFreq(float note)
{
  return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void pushHeld(uint8_t note)
{
  if (heldCount < KEY_COUNT) {
    heldNotes[heldCount++] = note;
  }
}

void popHeld(uint8_t note)
{
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

uint8_t currentHeldNote()
{
  return heldCount == 0 ? 0 : heldNotes[heldCount - 1];
}

void registerSequencerNote(uint8_t note)
{
  if (activeSequencerCount < MAX_ACTIVE_SEQ_NOTES) {
    activeSequencerNotes[activeSequencerCount++] = note;
  }
}

void unregisterSequencerNote(uint8_t note)
{
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

void clearActiveSequencerNotes()
{
  while (activeSequencerCount > 0) {
    uint8_t note = activeSequencerNotes[activeSequencerCount - 1];
    handleNoteOff(note);
    activeSequencerCount--;
  }
}

void handleNoteOn(uint8_t note)
{
  pushHeld(note);
  targetFreq = midiToFreq(static_cast<float>(note) + params.pitchOffset);
  envelope.noteOn();

  if (sequencerRecording && sequenceLength < MAX_SEQ_EVENTS) {
    sequenceBuffer[sequenceLength++] = {note, true, millis() - recordStartMs};
  }
}

void handleNoteOff(uint8_t note)
{
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

void clearSequence()
{
  clearActiveSequencerNotes();
  sequenceLength = 0;
  sequenceDuration = 0;
  playbackIndex = 0;
  activeSequencerCount = 0;
}

void beginRecording()
{
  sequencerPlaying = false;
  sequencerRecording = false;
  clearSequence();
  sequencerRecording = true;
  recordStartMs = millis();
}

void endRecording()
{
  sequencerRecording = false;
  if (sequenceLength > 0) {
    finalizeSequence();
  }
}

void startPlayback()
{
  if (sequenceLength == 0) return;
  sequencerPlaying = true;
  playbackIndex = 0;
  playbackStartMs = millis();
}

void stopPlayback()
{
  sequencerPlaying = false;
  playbackIndex = 0;
}

void triggerClick()
{
  clickSamplesRemaining = CLICK_LENGTH;
}

void updateSequencer()
{
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
    return;
  }
}

void updateRandomTrigger()
{
  if (randomNoteActive && millis() - randomNoteStart > 200) {
    handleNoteOff(randomNoteValue);
    randomNoteActive = false;
  }
}

void finalizeSequence()
{
  if (sequenceLength == 0) {
    sequenceDuration = 0;
    return;
  }
  for (uint16_t i = 0; i < sequenceLength; ++i) {
    sequenceOriginalTimestamp[i] = sequenceBuffer[i].timestamp;
  }
  sequenceDuration = sequenceOriginalTimestamp[sequenceLength - 1] + 1;
}

void resetPlaybackMarkers()
{
  playbackIndex = 0;
  playbackStartMs = millis();
  clearActiveSequencerNotes();
}

// --------------- keyboard scanning via MCP23017 ------------
void setupKeyboardExpander()
{
  keyboardExpander.begin_I2C(MCP_KEYBOARD_ADDR);

  // Configure rows as inputs with pull-ups, columns as outputs
  for (uint8_t col = 0; col < KEY_COLS; ++col) {
    keyboardExpander.pinMode(col, OUTPUT);
    keyboardExpander.digitalWrite(col, HIGH);
  }
  for (uint8_t row = 0; row < KEY_ROWS; ++row) {
    keyboardExpander.pinMode(8 + row, INPUT_PULLUP);
  }

  memset(lastKeyState, 0, sizeof(lastKeyState));
}

void scanKeyboard()
{
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

// --------------- switches via MCP23017 --------------------
void setupSwitchExpander()
{
  switchExpander.begin_I2C(MCP_SWITCH_ADDR);
  for (uint8_t i = 0; i < 8; ++i) {
    switchExpander.pinMode(i, INPUT_PULLUP);
  }
}

void readSwitches()
{
  bool recordPressed = !switchExpander.digitalRead(SWITCH_RECORD);
  bool playPressed   = !switchExpander.digitalRead(SWITCH_PLAY);
  bool clearPressed  = !switchExpander.digitalRead(SWITCH_CLEAR);
  bool holdPressed   = !switchExpander.digitalRead(SWITCH_HOLD);
  bool syncPressed   = !switchExpander.digitalRead(SWITCH_SYNC);
  bool randomPressed = !switchExpander.digitalRead(SWITCH_RANDOM);

  static bool lastRecord = false;
  static bool lastPlay = false;
  static bool lastClear = false;
  static bool lastHold = false;
  static bool lastSync = false;
  static bool lastRandom = false;

  if (recordPressed && !lastRecord) {
    if (sequencerRecording) {
      endRecording();
    } else {
      beginRecording();
    }
  }
  if (playPressed && !lastPlay) {
    if (sequencerPlaying) {
      stopPlayback();
      resetPlaybackMarkers();
    } else {
      resetPlaybackMarkers();
      startPlayback();
    }
  }
  if (clearPressed && !lastClear) {
    stopPlayback();
    sequencerRecording = false;
    clearSequence();
  }
  if (holdPressed && !lastHold) {
    if (heldCount > 0) {
      envelope.noteOff();
      heldCount = 0;
    }
  }
  if (syncPressed && !lastSync) {
    resetPlaybackMarkers();
  }
  if (randomPressed && !lastRandom) {
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

  lastRecord = recordPressed;
  lastPlay = playPressed;
  lastClear = clearPressed;
  lastHold = holdPressed;
  lastSync = syncPressed;
  lastRandom = randomPressed;
}

// ----------------------- MIDI input ------------------------
void handleMIDI()
{
  while (Serial1.available() > 0) {
    static uint8_t runningStatus = 0;
    uint8_t byte = Serial1.read();

    if (byte & 0x80) {
      runningStatus = byte;
      continue;
    }

    static uint8_t data1 = 0;
    static bool waitingForData2 = false;

    if (runningStatus >= 0x80 && runningStatus < 0xF0) {
      if (!waitingForData2) {
        data1 = byte;
        waitingForData2 = true;
      } else {
        waitingForData2 = false;
        uint8_t data2 = byte;
        uint8_t status = runningStatus & 0xF0;
        switch (status) {
          case 0x90:
            if (data2 > 0) {
              handleNoteOn(data1);
            } else {
              handleNoteOff(data1);
            }
            break;
          case 0x80:
            handleNoteOff(data1);
            break;
          default:
            break;
        }
      }
    }
  }
}

// -------------------- Analog controls ---------------------
float readNormalizedPot(uint8_t pin)
{
  uint16_t value = mozziAnalogRead(pin);
  return static_cast<float>(value) / ANALOG_MAX_VALUE;
}

void readAnalogs()
{
  params.waveMorph = readNormalizedPot(analogPins[0]) * 4.0f;
  params.envAttack = 5.0f + 500.0f * readNormalizedPot(analogPins[1]);
  params.envSustain = readNormalizedPot(analogPins[2]);
  params.envRelease = 20.0f + 1000.0f * readNormalizedPot(analogPins[3]);
  params.filterCutoff = 200.0f + 3200.0f * readNormalizedPot(analogPins[4]);
  params.filterResonance = 0.1f + 0.85f * readNormalizedPot(analogPins[5]);

  envelope.setAttack(params.envAttack);
  envelope.setDecay(0); // ASR behaviour
  envelope.setSustain(static_cast<uint8_t>(params.envSustain * 255.0f));
  envelope.setRelease(params.envRelease);

  lfoPitch.setFreq(params.lfoRate);
  lfoFilter.setFreq(params.lfoRate * 0.75f);
}

// -------------------- Wave morphing -----------------------
int16_t computeWaveSample()
{
  int16_t sinSample = oscSin.next();
  int16_t triSample = oscTri.next();
  int16_t sawSample = oscSaw.next();
  int16_t squareSample = oscSquare.next();

  // generate pulse via phasor and pulse width
  float morph = constrain(params.waveMorph, 0.0f, 4.0f);
  int region = static_cast<int>(morph);
  float blend = morph - region;

  float pulseWidth = 0.5f;
  if (region == 2) {
    pulseWidth = 0.1f + 0.8f * blend;
  } else if (region == 3) {
    pulseWidth = 0.9f - 0.4f * blend;
  }

  uint16_t phase = pulsePhasor.next(); // 0..65535
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

// ------------------- Display rendering --------------------
void renderWaveform(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
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

void renderSpectrum(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
  display.drawFrame(x, y, width, height);
  uint8_t bins = min<uint8_t>(width - 2, FFT_SAMPLES / 2);
  for (uint8_t i = 0; i < bins; ++i) {
    double magnitude = fftReal[i];
    int barHeight = static_cast<int>(log10(1 + magnitude) * (height - 2));
    barHeight = constrain(barHeight, 0, height - 2);
    display.drawLine(x + 1 + i, y + height - 1, x + 1 + i, y + height - 1 - barHeight);
  }
}

void updateDisplay()
{
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
  if (sequencerRecording) display.print("REC");
  else if (sequencerPlaying) display.print("PLAY");
  else display.print("STOP");
  display.print(" E:");
  display.print(sequenceLength);

  renderWaveform(64, 0, 63, 31);
  renderSpectrum(64, 32, 63, 31);

  display.sendBuffer();
}

// ------------------- FFT sample buffering -----------------
void pushSampleForFFT(int16_t sample)
{
  waveformBuffer[waveformWriteIndex] = sample;
  waveformWriteIndex = (waveformWriteIndex + 1) % FFT_SAMPLES;
}

void computeFFT()
{
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

// ------------------- Mozzi callbacks ----------------------
AudioOutput updateAudio()
{
  // Smooth frequency glide to avoid clicks
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
    int16_t clickValue = static_cast<int16_t>((clickSamplesRemaining & 1 ? 1 : -1) * env * 6000.0f);
    finalSample = constrain(finalSample + clickValue, -32767, 32767);
    clickSamplesRemaining--;
  }

  pushSampleForFFT(finalSample);

  return MonoOutput::from16Bit(finalSample);
}

void updateControl()
{
  readAnalogs();
  scanKeyboard();
  readSwitches();
  handleMIDI();
  updateSequencer();
  updateRandomTrigger();
  updateDisplay();
  computeFFT();
}

// --------------------------- setup ------------------------
void setup()
{
  Wire.begin();
  display.begin();
  display.clearBuffer();
  display.sendBuffer();

  randomSeed(analogRead(analogPins[0]));

  setupKeyboardExpander();
  setupSwitchExpander();

  Serial1.begin(31250);

  envelope.setAttack(params.envAttack);
  envelope.setDecay(0);
  envelope.setSustain(static_cast<uint8_t>(params.envSustain * 255.0f));
  envelope.setRelease(params.envRelease);

  lfoPitch.setFreq(params.lfoRate);
  lfoFilter.setFreq(params.lfoRate * 0.75f);

  startMozzi(CONTROL_RATE);
}

void loop()
{
  audioHook();
}
