#include "midi_input.h"

#include "sequencer.h"

#include <Arduino.h>

void handleMIDI() {
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
