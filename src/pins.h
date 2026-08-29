#pragma once

// Every pin the rover uses, and nothing else. The wiring is one contract: a pin
// appears here or it is not wired.

#include <Arduino.h>

// GPIO 16 and 17 are not broken out on this 30-pin board, which is why the
// brake light is on 19 and the status LED on 13 rather than the datasheet pins.
constexpr uint8_t PIN_BRAKE_LIGHT = 19;
constexpr uint8_t PIN_STATUS_LED = 13;
constexpr uint8_t PIN_MODE_BUTTON = 23;

// An active buzzer, with its own oscillator, behind a transistor on the module.
// Holding the pin at the sounding level is enough; tone() or PWM would only
// fight the oscillator it already has.
constexpr uint8_t PIN_BUZZER = 4;

// Active low, settled by test rather than assumption - these modules are sold
// in both polarities. The consequence: an undriven pin is not a quiet one. It
// sounds from power-on until the firmware silences it, so a crashed board
// announces itself and silence depends on the firmware being alive.
constexpr uint8_t BUZZER_SOUND = LOW;
constexpr uint8_t BUZZER_SILENT = HIGH;

// Four motors on two channels: the two on each side are wired in parallel into
// one output pair, so the driver and the code only ever see two sides. ENA/ENB
// carry the PWM that sets speed; the IN pairs set direction.
constexpr uint8_t PIN_MOTOR_ENA = 32; // left side speed
constexpr uint8_t PIN_MOTOR_IN1 = 33; // left side direction
constexpr uint8_t PIN_MOTOR_IN2 = 25;
constexpr uint8_t PIN_MOTOR_IN3 = 26; // right side direction
constexpr uint8_t PIN_MOTOR_IN4 = 27;
constexpr uint8_t PIN_MOTOR_ENB = 14; // right side speed

// ADC1 only. ADC2 shares hardware with the radio and cannot be read while Wi-Fi
// is running, which confines every analog sensor to GPIO 32-39. Only the analog
// outputs are wired: lane keeping has to know how far a sensor has gone into a
// stripe, not merely that it has.
//
// A HIGH value means a DARK surface - the opposite of the intuitive reading,
// and inverting it is the easiest mistake to make here. Inside the lane both
// sensors read low; a high one has reached a stripe.
//
// The two are not interchangeable. They read a few hundred counts apart over
// the same white, and the offset moves between builds, so nothing may assume a
// raw difference of zero means straight. The calibration measures it per run.
// Figures and method: docs/journal.md, session 12.
constexpr uint8_t PIN_LINE_SENSOR_LEFT = 34;
constexpr uint8_t PIN_LINE_SENSOR_RIGHT = 35;

// A 5 V part fed from VIN, so ECHO swings above what GPIO 18 tolerates and
// arrives through a 1k/2k divider - 3.42 V for a 5.1 V pulse. TRIG needs none:
// the module reads a 3.3 V output as HIGH.
//
// Mounted on the upper deck, and that is calibration rather than tidiness. From
// the lower deck the beam spread reached the floor at a shallow angle and came
// back, reading as an obstacle on clear ground.
constexpr uint8_t PIN_ULTRASONIC_TRIG = 5;
constexpr uint8_t PIN_ULTRASONIC_ECHO = 18;

// Named rather than left to the core's default, which the ESP32 lets move.
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;
