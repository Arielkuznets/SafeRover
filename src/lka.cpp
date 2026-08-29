#include "lka.h"

#include "aeb.h"
#include "config.h"
#include "modes.h"
#include "pins.h"
#include "sensors.h"

CalPhase calPhase = CalPhase::Idle;
static unsigned long calPhaseStartMs = 0;

// Why the last calibration was refused. Failed on its own does not say which of
// the two things went wrong, and they have opposite fixes: the rover is
// standing in the wrong place, or it was not standing still.
//
// A pointer to a literal rather than a String - the strings are compile-time
// constants that outlive every caller.
const char *calFailReason = "";

// zeroPoint is the difference the two sensors show over the same white, and it
// is not zero: the parts are not identical, so a rover sitting straight reads a
// few hundred counts of apparent lean. Measuring that lie and subtracting it is
// the entire purpose of the calibration.
//
// Measured per session rather than written down as a constant because it moves
// - the same pair read 274 in one room and 120 in another.
//
// The two whites serve the off-lane guard alone. Nothing in the control law
// reads them: a difference is what steers.
int16_t zeroPoint = 0;
static uint16_t calLeftWhite = 0;
static uint16_t calRightWhite = 0;

// Accumulated across the sampling window and divided at the end. The difference
// is signed because it is normally negative; the sides are summed separately
// because the guard needs each sensor's own white, not their average.
static int32_t calDiffSum = 0;
static uint32_t calLeftSum = 0;
static uint32_t calRightSum = 0;
static uint16_t calSampleCount = 0;

// The last error the controller computed, for the dashboard. Reporting only.
int32_t lkaError = 0;

// The extremes of the RAW error since the dashboard last asked, recorded before
// the deadband flattens it. The value above is sampled twice a second after the
// deadband has already zeroed it, so a signal swinging between -190 and +190
// reports as a flat zero - the instrument cannot see what it is pointed at.
// These let a 2 Hz poll describe a 12 Hz loop.
//
// The flag stops a window with no samples reporting the previous window's
// numbers as though they were fresh.
int32_t lkaErrorMin = 0;
int32_t lkaErrorMax = 0;
bool lkaErrorSeen = false;

// Live so they can be tuned from the phone without a rebuild. Kp tuning is
// dozens of trials, and through a rebuild it simply does not get finished. Once
// found, the values belong back in config.h as the defaults.
uint8_t lkaKp = LKA_KP_DEFAULT;
uint8_t lkaBasePercent = LKA_BASE_PERCENT_DEFAULT;

// Derived from the phase rather than kept as a flag. A flag would have to be
// cleared on every way out - the normal end, the abort, any path added later -
// and one missed clear leaves a stale claim behind.
bool calibrationRunning() {
  return calPhase == CalPhase::Settling || calPhase == CalPhase::Sampling;
}

// Begins a calibration.
//
// Nothing switches steering off here. Setting the phase away from Ready is what
// stops it, because lkaSteering() is derived from that phase - so the rover
// cannot drive through its own measurement by construction rather than by this
// function remembering to prevent it.
//
// The accumulators are cleared here rather than merely read at the end: they
// hold whatever the sensors have seen since boot, which includes the rover
// being carried or set down on a dark surface.
void startCalibration() {
  // Cleared here, not at the failure, so a refusal stays readable for as long
  // as it is the last thing that happened.
  calFailReason = "";

  lineGapMaxMs = 0;

  calDiffSum = 0;
  calLeftSum = 0;
  calRightSum = 0;
  calSampleCount = 0;

  calPhase = CalPhase::Settling;
  calPhaseStartMs = millis();
}

// The phase as a word, for the dashboard.
const char *calPhaseName(CalPhase phase) {
  switch (phase) {
  case CalPhase::Settling:
    return "settling";
  case CalPhase::Sampling:
    return "sampling";
  case CalPhase::Ready:
    return "ready";
  case CalPhase::Failed:
    return "failed";
  default:
    return "idle";
  }
}

// Adds one reading to the window, called from the line read block once the
// values have been taken. Nothing is measured here.
void accumulateCalibrationSample() {
  if (calPhase != CalPhase::Sampling) {
    return;
  }

  calDiffSum += static_cast<int32_t>(lineLeftValue) -
                static_cast<int32_t>(lineRightValue);
  calLeftSum += lineLeftValue;
  calRightSum += lineRightValue;
  calSampleCount++;
}

// Refuses the calibration and says why. Failed rather than Idle, because Idle
// means nothing was attempted and this means something was and was refused.
static void failCalibration(const char *reason) {
  calPhase = CalPhase::Failed;
  calFailReason = reason;

  // A refusal must not be mistakable for a button press that did not register -
  // the operator would press again and fail again. Sounded here so it happens
  // however the calibration was started, including from the dashboard where
  // there is no mode to drop out of.
  startAlert();
}

// Turns the window into a zero point, or refuses it.
//
// Both checks read a difference or a spread and never a level, which is what
// lets them be fixed numbers: levels move by more than a thousand counts
// between rooms, and every guard this project built on one had to be thrown
// away.
static void evaluateCalibration() {
  if (calSampleCount == 0) {
    failCalibration("no samples");
    return;
  }

  const int16_t restingDiff = static_cast<int16_t>(calDiffSum / calSampleCount);
  const uint16_t leftWhite = static_cast<uint16_t>(calLeftSum / calSampleCount);
  const uint16_t rightWhite =
      static_cast<uint16_t>(calRightSum / calSampleCount);

  // A difference far past the part mismatch means one sensor is over a stripe,
  // and a zero point measured there teaches the rover that crooked is straight.
  if (abs(restingDiff) > CAL_MAX_RESTING_DIFF) {
    failCalibration("one sensor is not on white");
    return;
  }

  // Hundreds of counts of spread means the rover was held, nudged or still
  // rocking. An average across that is a reasonable-looking number describing
  // nothing.
  const uint16_t leftSpread = lineLeftMax - lineLeftMin;
  const uint16_t rightSpread = lineRightMax - lineRightMin;

  if (leftSpread > CAL_MAX_SPREAD || rightSpread > CAL_MAX_SPREAD) {
    failCalibration("rover was not still");
    return;
  }

  zeroPoint = restingDiff;
  calLeftWhite = leftWhite;
  calRightWhite = rightWhite;

  // Nothing here decides to drive. Ready is a measurement result; whether it
  // becomes movement is lkaSteering()'s question, and only AUTO answers yes.
  calPhase = CalPhase::Ready;
}

// Idle rather than Failed: nothing was judged, the measurement was abandoned.
void abortCalibration() { calPhase = CalPhase::Idle; }

// Steps the calibration one loop pass at a time. Nothing here blocks and
// nothing here moves - the rover stands still for the whole of it.
void updateCalibration() {
  if (!calibrationRunning()) {
    return;
  }

  const unsigned long now = millis();

  if (calPhase == CalPhase::Settling) {
    if (now - calPhaseStartMs >= CAL_SETTLE_MS) {
      // Cleared at this transition, not at the start. The spread has to
      // describe the sampling window alone; reset any earlier it would carry
      // the rocking that settling exists to wait out, and the stability check
      // would refuse every honest calibration.
      lineLeftMin = ADC_MAX_VALUE;
      lineLeftMax = 0;
      lineRightMin = ADC_MAX_VALUE;
      lineRightMax = 0;

      calPhase = CalPhase::Sampling;
      calPhaseStartMs = now;
    }
    return;
  }

  if (now - calPhaseStartMs >= CAL_SAMPLE_MS) {
    evaluateCalibration();
  }
}

// The one question a difference cannot answer: both sensors on white and both
// on black look identical to a subtraction, and the second means the rover has
// left the track. Levels are the only thing that separates them.
//
// A question, not an action. This used to stop the rover itself, which made the
// controller depend on the state machine that starts it - and enterMode calls
// startCalibration, so each needed the other. The loop asks and decides.
bool lkaOffLane() {
  return lineLeftValue > calLeftWhite + LKA_OFF_LANE_MARGIN &&
         lineRightValue > calRightWhite + LKA_OFF_LANE_MARGIN;
}

// The lane keeping controller.
//
// It writes commandedLeft and commandedRight and stops there - it calls neither
// drive nor safeDrive. This records intent exactly as a request handler does,
// and the single write to the motors stays in the loop, after the safety layer
// has had its say.
//
// Gated by the caller, not here. The loop runs this only while lkaSteering()
// holds, which keeps the mode out of this file entirely.
void updateLka() {
  const int32_t diff =
      static_cast<int32_t>(lineLeftValue) - static_cast<int32_t>(lineRightValue);

  // The measured lie is subtracted here and nowhere else. What is left is lean.
  int32_t error = diff - zeroPoint;

  // Recorded above the deadband, because this is the last point at which the
  // signal still exists.
  if (!lkaErrorSeen) {
    lkaErrorMin = error;
    lkaErrorMax = error;
    lkaErrorSeen = true;
  } else {
    lkaErrorMin = min(lkaErrorMin, error);
    lkaErrorMax = max(lkaErrorMax, error);
  }

  // Inside the lane neither sensor has reached a stripe, but a difference
  // always has some value - noise, drift, the ADC. Without this the rover would
  // steer against its own measurement error the whole way down a straight lane.
  if (abs(error) < LKA_DEADBAND) {
    error = 0;
  }

  lkaError = error;

  const int32_t correction =
      (static_cast<int32_t>(lkaKp) * error) / LINE_DIFF_FULL_SCALE;

  // Which side gets the extra, derived rather than guessed. High means dark, so
  // drifting left raises the left reading, so left minus right rises, so the
  // error is positive. Recovering means turning right, and a differential drive
  // turns towards its slower side - the right loses speed and the left gains
  // it.
  //
  // Confirm the sign on a stand before the wheels touch the floor. Reversed, it
  // steers out of the lane instead of back into it, and from the driving seat
  // that looks exactly like a badly chosen Kp.
  const int32_t base = static_cast<int32_t>(dutyFromPercent(lkaBasePercent));
  const int32_t limit = static_cast<int32_t>(PWM_MAX_DUTY);

  commandedLeft =
      static_cast<int16_t>(constrain(base + correction, -limit, limit));
  commandedRight =
      static_cast<int16_t>(constrain(base - correction, -limit, limit));

  // Nothing here touches lastCommandMs. It used to, which kept the watchdog
  // permanently fed and was the only reason it never fired in autonomous
  // driving - an accident of the controller looking like a command source. The
  // watchdog is now gated on MANUAL, where the rule is stated outright.
}
