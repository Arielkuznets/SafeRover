#pragma once

// Autonomous emergency braking, and the one door to the motors.
//
// safeDrive() is the only thing any driving decision may call. The stages are
// enforced inside it, so no navigation code can route around them - a safety
// layer that can be bypassed is not a safety layer.

#include <Arduino.h>

// One value, not a flag per stage: with four booleans there is a state where
// Warn and Stop are both set and something has to decide which wins. The order
// is compared directly - larger is more severe - so keep it.
enum class AebStage : uint8_t { Clear, Warn, Slow, Stop };

extern AebStage aebStage;

// The worst stage enforced since the dashboard last asked. Reset on read there.
extern AebStage aebPeakStage;

const char *aebStageName(AebStage stage);

void updateAebStage();
void confirmWarnStage();

void safeDrive(int16_t left, int16_t right);

// Sounds the buzzer for something that is not the AEB - a refused calibration,
// or the lane keeping stopping itself. It lives here because the buzzer does:
// applyAebOutputs writes that pin every pass, so anything written elsewhere
// would be gone within a millisecond.
void startAlert();

void aebBegin();
