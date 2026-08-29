#pragma once

// The readings, and the code that takes them. A pure provider: nothing here
// decides anything, and nothing calls back into the layers above.

#include <Arduino.h>

// The last readings the loop took. Held here so a request handler can report
// them without measuring anything itself - reading a sensor inside a handler
// would let a browser decide when pulseIn fires.
//
// echoDurationValue keeps the meaning zero carries everywhere in this project:
// no echo came back, which is not a distance.
extern uint16_t lineLeftValue;
extern uint16_t lineRightValue;
extern unsigned long echoDurationValue;

// The span each line sensor has seen, kept per sensor: a shared span would let
// a sensor that reached a stripe cover for one that never did.
extern uint16_t lineLeftMin;
extern uint16_t lineLeftMax;
extern uint16_t lineRightMin;
extern uint16_t lineRightMax;

// When the loop last read each thing, and how well it managed it. The AEB reads
// lastDistanceRead to decide whether its reading has gone stale.
extern unsigned long lastLineRead;
extern unsigned long lastLineSummary;
extern unsigned long lastDistanceRead;
extern uint16_t lineReadsInWindow;
extern unsigned long lineGapMaxMs;
extern unsigned long lineAvgGapMs;

uint16_t readLineAveraged(uint8_t pin);
unsigned long readEchoMedian();
float distanceFromDuration(unsigned long durationUs);

void sensorsBegin();
