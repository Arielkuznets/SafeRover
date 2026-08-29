#include "motors.h"

#include "config.h"
#include "pins.h"

// Sets both sides at once. Each argument carries speed in its magnitude and
// direction in its sign, so one number per side says everything about that
// side.
//
// The two direction pins of a side are always written as a pair, at opposite
// levels. Setting both to the same level brakes that side rather than turning
// it. Which pattern means forward depends on which way round the motor leads
// were connected.
//
// The third argument has no default: every caller has to state what a stop
// means. Braking is a whole-vehicle action, so it ignores the two speeds rather
// than pretending to mix with them.
void drive(int16_t left, int16_t right, StopMode stop) {
  if (stop == StopMode::Brake) {
    // Both direction pins of a side at the same level short that motor through
    // the bridge, and its own momentum works against it. The enable has to stay
    // high for any of that to reach the windings: at duty zero the outputs are
    // disconnected and the wheels roll on whatever the direction pins say -
    // which is exactly what coasting is.
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
    digitalWrite(PIN_MOTOR_IN3, LOW);
    digitalWrite(PIN_MOTOR_IN4, LOW);

    ledcWrite(PIN_MOTOR_ENA, PWM_MAX_DUTY);
    ledcWrite(PIN_MOTOR_ENB, PWM_MAX_DUTY);
    return;
  }

  const bool leftForward = (left >= 0);
  digitalWrite(PIN_MOTOR_IN1, leftForward ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_IN2, leftForward ? LOW : HIGH);

  const bool rightForward = (right >= 0);
  digitalWrite(PIN_MOTOR_IN3, rightForward ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_IN4, rightForward ? LOW : HIGH);

  // The sign has been consumed by the direction pins, so only the magnitude is
  // left for the speed. abs() on the full negative range of int16_t would
  // overflow, but these values are bounded by PWM_MAX_DUTY long before that.
  const uint32_t leftDuty = static_cast<uint32_t>(leftForward ? left : -left);
  const uint32_t rightDuty =
      static_cast<uint32_t>(rightForward ? right : -right);

  ledcWrite(PIN_MOTOR_ENA, min(leftDuty, PWM_MAX_DUTY));
  ledcWrite(PIN_MOTOR_ENB, min(rightDuty, PWM_MAX_DUTY));
}

// The stop at the end is not decoration: the direction pins power up in an
// undefined state, and four motors on a chassis with no caster will happily
// drive off a bench while the rest of setup() is still running. Coast rather
// than brake - there is no momentum to hold yet, and braking would drive both
// enable pins for the whole of setup.
void motorsBegin() {
  ledcAttachChannel(PIN_MOTOR_ENA, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_MOTOR_LEFT);
  ledcAttachChannel(PIN_MOTOR_ENB, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_MOTOR_RIGHT);

  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  pinMode(PIN_MOTOR_IN3, OUTPUT);
  pinMode(PIN_MOTOR_IN4, OUTPUT);

  drive(0, 0, StopMode::Coast);
}
