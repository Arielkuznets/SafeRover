#pragma once

// Lane keeping: the calibration that measures the rover's own bias, and the
// proportional controller that steers against what is left.
//
// Nothing here moves a wheel. updateLka records intent into the commanded
// values exactly as a request handler does, and the loop applies it through the
// safety layer.

#include <Arduino.h>

// Where the calibration has got to. The rover does not move during any of it:
// the measurement is the resting difference between the two sensors, taken
// standing still on white, and it needs no stripe at all. Settling is separate
// from Sampling because a hand has just let go of the rover, and readings taken
// then are readings of a grip.
//
// Idle covers both "never calibrated" and "aborted". Failed is kept apart from
// both: Idle means nothing was attempted, Failed means something was and was
// refused, and the two have different fixes.
//
// Ready lasts only until AUTO is left, and that is a measured decision rather
// than a cautious one. The resting difference came out -101 on one calibration
// and -314 on another with nothing in the mounting touched between them - a
// spread of 213 counts against a deadband of 200. A zero point that moves that
// much standing still cannot be trusted to survive being driven.
enum class CalPhase : uint8_t { Idle, Settling, Sampling, Ready, Failed };

extern CalPhase calPhase;
extern const char *calFailReason;
extern int16_t zeroPoint;

extern int32_t lkaError;
extern int32_t lkaErrorMin;
extern int32_t lkaErrorMax;
extern bool lkaErrorSeen;

extern uint8_t lkaKp;
extern uint8_t lkaBasePercent;

bool calibrationRunning();
const char *calPhaseName(CalPhase phase);

void startCalibration();
void abortCalibration();
void accumulateCalibrationSample();
void updateCalibration();

bool lkaOffLane();
void updateLka();
