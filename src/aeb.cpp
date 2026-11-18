#include "aeb.h"

#include "config.h"
#include "motors.h"
#include "pins.h"
#include "sensors.h"

// Starts Clear because nothing has been measured yet, not because the road is
// known to be empty. The first reading replaces it.
AebStage aebStage = AebStage::Clear;
static uint8_t aebMissedReads = 0;
static uint8_t aebWarnReads = 0;

// The last distance that actually came back, because a missing echo means
// different things depending on what preceded it. Started just outside the warn
// threshold: before any reading exists the assumption is open road.
static float aebLastValidCm = AEB_WARN_CM + 1.0f;

// The worst stage enforced since the dashboard last asked, reset on read there.
// The page polls every 500 ms and a braking event lasts about 50, so a
// dashboard reading only the live stage would miss most of them.
AebStage aebPeakStage = AebStage::Clear;

// A reason to sound the buzzer that is not the AEB. Held as a start time rather
// than a deadline, so the comparison cannot wrap.
static unsigned long alertStartMs = 0;
static bool alertRaised = false;

// It cannot write the pin itself: applyAebOutputs sets the buzzer every pass,
// so anything written here would be gone within a millisecond. It raises a
// condition that applyAebOutputs reads instead.
void startAlert() {
  alertStartMs = millis();
  alertRaised = true;
}

static bool alertSounding() {
  return alertRaised && (millis() - alertStartMs < ALERT_BEEP_MS);
}

// Drives the two warning outputs from the stage that was actually enforced -
// passed in rather than read from the global, so a stage forced by a stale
// reading is announced too.
//
// Both are gated on the operator asking for movement: a rover parked near a
// wall has nothing to announce. The brake light follows only the stages that
// actually slow the rover, the same as a car - lamps mean deceleration.
static void applyAebOutputs(bool movingForward, AebStage stage) {
  // Warn has to have held for a while; the two stages that act on the motors
  // announce themselves the moment they are entered.
  const bool confirmed =
      stage != AebStage::Warn || aebWarnReads >= AEB_WARN_CONFIRM_READS;

  const bool sounding = movingForward && stage != AebStage::Clear && confirmed;
  const bool braking =
      movingForward && (stage == AebStage::Slow || stage == AebStage::Stop);

  if (movingForward && stage > aebPeakStage) {
    aebPeakStage = stage;
  }

  // The buzzer has two owners. Either can sound it; only both being quiet
  // silences it.
  const bool buzzing = sounding || alertSounding();

  digitalWrite(PIN_BUZZER, buzzing ? BUZZER_SOUND : BUZZER_SILENT);
  ledcWrite(PIN_BRAKE_LIGHT, braking ? PWM_MAX_DUTY : 0);
}

// Manual control and the lane keeping both come through here, so the AEB
// intercepts both without touching the code that decides where to go. The one
// exception is the drive() in motorsBegin - hardware initialisation, at a
// moment when there is no reading to reason about.
void safeDrive(int16_t left, int16_t right) {
  // Its own clock rather than one handed in, so a caller cannot pass a stale
  // timestamp and talk this layer out of the check.
  const bool readingStale =
      (millis() - lastDistanceRead) > AEB_READING_MAX_AGE_MS;

  // A missing echo can honestly mean open road. A reading that has stopped
  // arriving at all cannot mean anything but a fault, and a fault in the
  // braking layer fails towards stopping.
  const AebStage stage = readingStale ? AebStage::Stop : aebStage;

  // Heading at it, not merely moving - the sensor faces forward, so reversing
  // away from an obstacle is the safest thing an operator can do.
  //
  // Deliberately NOT the same test as the escape rule below. That asks whether
  // any side is negative, this whether any is positive, and a pivot answers yes
  // to both: never limited, but still announced. It looks like a bug and is
  // not.
  applyAebOutputs(left > 0 || right > 0, stage);

  // Nothing asked for, so there is nothing to hold back. Braking a stationary
  // rover arrests nothing and would hold both enable pins at full duty, leaving
  // the L298N conducting for no reason.
  if (left == 0 && right == 0) {
    drive(0, 0, StopMode::Coast);
    return;
  }

  // Reverse and pivot turns are escapes and are never limited. A rover pinned
  // against a wall by its own safety layer, with the commands that would free
  // it refused, is a worse failure than the one being prevented.
  if (left < 0 || right < 0) {
    drive(left, right, StopMode::Coast);
    return;
  }

  if (stage == AebStage::Stop) {
    drive(0, 0, StopMode::Brake);
    return;
  }

  if (stage == AebStage::Slow) {
    // A ceiling, never an assignment. A command already slower passes through
    // untouched: this layer may only ever take speed away.
    const int16_t cap = static_cast<int16_t>(dutyFromPercent(AEB_SLOW_PERCENT));
    left = min(left, cap);
    right = min(right, cap);
  }

  drive(left, right, StopMode::Coast);
}

// All four listed rather than a default, so a stage added later warns here
// instead of arriving as something else.
const char *aebStageName(AebStage stage) {
  switch (stage) {
  case AebStage::Clear:
    return "CLEAR";
  case AebStage::Warn:
    return "WARN";
  case AebStage::Slow:
    return "SLOW";
  case AebStage::Stop:
    return "STOP";
  }
  return "?";
}

// Picks the stage from the reading the loop has just taken, once per
// measurement rather than once per pass. It decides and does nothing else:
// enforcement belongs inside safeDrive, where no caller can route around it.
void updateAebStage() {
  if (echoDurationValue == 0) {
    aebMissedReads++;

    // Under the limit the stage is held. One lost echo is the absence of
    // evidence, not evidence that anything ahead changed.
    if (aebMissedReads < AEB_MISSED_READS_LIMIT) {
      return;
    }

    // Saturated, so a long outage cannot wrap the counter and hand the
    // tolerance back from zero.
    aebMissedReads = AEB_MISSED_READS_LIMIT;

    // Sustained silence means opposite things depending on what came before.
    // Losing the echo at three metres is open road - the module only reaches
    // about four. Losing it at fifteen centimetres is something close, possibly
    // inside the blind zone under 2 cm. The hole this leaves is a sensor that
    // dies while the road really is empty; closing it would mean stopping in
    // every open room.
    aebStage =
        (aebLastValidCm <= AEB_WARN_CM) ? AebStage::Stop : AebStage::Clear;
  } else {
    aebMissedReads = 0;

    // Scoped to this branch: the other has no distance to speak of, and a
    // variable above both would need a placeholder that means nothing.
    const float distanceCm = distanceFromDuration(echoDurationValue);
    aebLastValidCm = distanceCm;

    AebStage byDistance = AebStage::Clear;
    if (distanceCm <= AEB_STOP_CM) {
      byDistance = AebStage::Stop;
    } else if (distanceCm <= AEB_SLOW_CM) {
      byDistance = AebStage::Slow;
    } else if (distanceCm <= AEB_WARN_CM) {
      byDistance = AebStage::Warn;
    }

    if (byDistance > aebStage) {
      // Escalation skips nothing. Something appearing inside the stop threshold
      // goes straight there rather than stepping down over three readings,
      // which at full power would be a quarter of a metre.
      aebStage = byDistance;
    } else {
      // Leaving takes one stage per reading, and only past the exit threshold.
      switch (aebStage) {
      case AebStage::Stop:
        if (distanceCm > AEB_STOP_EXIT_CM) {
          aebStage = AebStage::Slow;
        }
        break;
      case AebStage::Slow:
        if (distanceCm > AEB_SLOW_EXIT_CM) {
          aebStage = AebStage::Warn;
        }
        break;
      case AebStage::Warn:
        if (distanceCm > AEB_WARN_EXIT_CM) {
          aebStage = AebStage::Clear;
        }
        break;
      case AebStage::Clear:
        break;
      }
    }
  }
}

// Counts how long Warn has held. Kept out of updateAebStage so the stage
// machine stays the one thing that decides the stage, and called beside it
// because it counts readings rather than loop passes.
void confirmWarnStage() {
  if (aebStage != AebStage::Warn) {
    aebWarnReads = 0;
    return;
  }

  // Saturated, so a long stretch at Warn cannot wrap the counter back under the
  // limit and silence a warning that had earned it.
  if (aebWarnReads < AEB_WARN_CONFIRM_READS) {
    aebWarnReads++;
  }
}

// The motor channels join this channel's timer (same frequency and
// resolution, see config.h), so changing the motor frequency would change
// the brake light's too.
void aebBegin() {
  ledcAttachChannel(PIN_BRAKE_LIGHT, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_BRAKE);
}
