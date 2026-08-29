#pragma once

// The motor driver, and the only code that knows an L298N exists.
//
// Nothing outside the safety layer may call drive(). It is declared here
// because aeb.cpp needs it; every other caller goes through safeDrive().

#include <Arduino.h>

// What a stop means at the motors. Coasting drops the duty to zero and lets the
// wheels roll on; braking shorts each motor through the bridge and holds it.
enum class StopMode : uint8_t { Coast, Brake };

void drive(int16_t left, int16_t right, StopMode stop);

// Attaches the PWM channels and the direction pins, and parks the motors.
void motorsBegin();
