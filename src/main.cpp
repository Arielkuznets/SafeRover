// SafeRover - the scheduler. Everything else lives in a module; what is left
// here is the non-blocking millis() loop that decides when each of them runs,
// and the single write to the motors every driving decision passes through.
//
// No delay() past setup(): a delay is a period of blindness, in which the
// sensors stop being read and the safety layer stops being asked.

#include <Arduino.h>

#include "aeb.h"
#include "cloud.h"
#include "config.h"
#include "display.h"
#include "lka.h"
#include "modes.h"
#include "motors.h"
#include "pins.h"
#include "sensors.h"
#include "web.h"

unsigned long lastWebHandle = 0;
unsigned long lastDisplayCheck = 0;

// Brings the hardware up. Each module raises its own pins; what is left here is
// what belongs to no module.
void setup() {
  // Before anything else: the buzzer sounds while its pin is undriven, so every
  // instruction ahead of this one is spent making noise.
  //
  // The order cannot be inverted. A write to a pin that has not been configured
  // is dropped rather than queued, so the AVR habit of setting the latch ahead
  // of pinMode silently does nothing here.
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);

  aebBegin();
  modesBegin();
  motorsBegin();
  sensorsBegin();
  displayBegin();

  startAccessPoint();
  startWebServer();

  // A power-on self test, and a permanent one. The buzzer's VCC is unplugged by
  // hand before every upload, and anything unplugged by hand can be left
  // unplugged - the AEB would then warn in silence with nothing wrong in the
  // code to find. This beep says the wire went back.
  digitalWrite(PIN_BUZZER, BUZZER_SOUND);
  delay(BUZZER_TEST_MS);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);
}

void loop() {
  const unsigned long now = millis();

  // First on purpose: on the rare pass where it does redraw it blocks for tens
  // of milliseconds, and the distance read afterwards is then the fresher one.
  if (now - lastDisplayCheck >= DISPLAY_CHECK_INTERVAL_MS) {
    lastDisplayCheck = now;
    updateStatusScreen();
  }

  // The button is the mode selector's input, so it is read where the mode is.
  // Ignored across an upload: the access point is down, the rover is parked,
  // and every phase of an upload is bounded in time, so there is nothing to
  // escape from and nothing to change mode for.
  //
  // uploadBusy() rather than uploadRunning(), because the request is answered a
  // moment before the work starts. Without it a press inside that grace could
  // leave STANDBY, and an upload guarded on STANDBY would begin from MANUAL.
  if (!uploadBusy()) {
    updateModeButton(now);
  }

  // Data collection only: read and accumulate, no thresholds and no decisions.
  if (now - lastLineRead >= LINE_READ_INTERVAL_MS) {
    // Measured before the timestamp is overwritten: the useful number is the
    // interval that actually happened, not the one asked for. The first read
    // has nothing to measure against - the whole of setup() would be recorded
    // as one gap and sit in the maximum for the rest of the run.
    if (lastLineRead != 0) {
      const unsigned long gap = now - lastLineRead;

      if (gap > lineGapMaxMs) {
        lineGapMaxMs = gap;
      }
    }

    lastLineRead = now;
    lineReadsInWindow++;

    lineLeftValue = readLineAveraged(PIN_LINE_SENSOR_LEFT);
    lineRightValue = readLineAveraged(PIN_LINE_SENSOR_RIGHT);

    // Built as a diagnostic, and now also what the calibration's stability
    // check reads: a rover nudged while measured shows a spread of hundreds.
    lineLeftMin = min(lineLeftMin, lineLeftValue);
    lineLeftMax = max(lineLeftMax, lineLeftValue);
    lineRightMin = min(lineRightMin, lineRightValue);
    lineRightMax = max(lineRightMax, lineRightValue);

    // Inside the read block, so the window counts readings and not loop passes.
    accumulateCalibrationSample();
  }

  // The first window is discarded: it spans the whole of setup(), so its
  // average would describe the boot rather than the loop.
  if (lastLineSummary == 0) {
    lastLineSummary = now;
    lineReadsInWindow = 0;
  } else if (now - lastLineSummary >= LINE_SUMMARY_INTERVAL_MS) {
    const unsigned long elapsed = now - lastLineSummary;
    const uint16_t reads = lineReadsInWindow;

    lastLineSummary = now;
    lineReadsInWindow = 0;

    // A window with no reads has no average, and would divide by zero.
    if (reads > 0) {
      lineAvgGapMs = elapsed / reads;
    }
  }

  // Median-filtered not to smooth a wobble - the raw signal is steady to within
  // a tenth of a percent - but for the occasional sample that comes back badly
  // wrong, which is what a moving vehicle produces.
  if (now - lastDistanceRead >= DISTANCE_READ_INTERVAL_MS) {
    lastDistanceRead = now;

    // Stored as the raw pulse, so zero keeps the meaning it carries everywhere
    // here: no echo came back. Converting it would produce 0.0 cm, which reads
    // exactly like an obstacle against the sensor.
    echoDurationValue = readEchoMedian();

    // Inside the read block, so the stage is reconsidered only when there is
    // something new to reconsider it from.
    updateAebStage();
    confirmWarnStage();

    // Inside the read block for the same reason the two above are: aebStage
    // can only change here, so an observer running at this cadence cannot miss
    // a transition. Running it in the loop body would let a stop that came and
    // went between two checks vanish from the log.
    updateCloud();
  }

  // Advances a phase and nothing else: it writes no command and moves no wheel.
  updateCalibration();

  // Here rather than in a module: updateLka would have to call enterMode, and
  // enterMode calls startCalibration, so the controller and the state machine
  // would each need the other. The loop is allowed to know about both.
  //
  // A refused calibration is not a state to drive in, and the drop to STANDBY
  // makes the rover safe while the reason is read off the dashboard.
  if (mode == Mode::Auto && calPhase == CalPhase::Failed) {
    enterMode(Mode::Standby);
  }

  // After the calibration, so a measurement that just finished steers on the
  // same pass. Before safeDrive, so what it writes is judged like any command.
  if (lkaSteering()) {
    if (lkaOffLane()) {
      // Off the track, so the zero point describes somewhere the rover no
      // longer is. It sounds, because an operator watching the floor has to be
      // told.
      startAlert();
      enterMode(Mode::Standby);
    } else {
      updateLka();
    }
  }

  // The watchdog. Silence is read as a stop, and the flag guards the condition
  // rather than letting it fire every pass once the deadline is past.
  //
  // millis() here, NOT the loop's cached now, and this is not cosmetic - the
  // line read `now` for a day and the rover was unusable. Every other schedule
  // compares now against a stamp written as `= now`, which can never be newer.
  // lastCommandMs is the exception: a request handler writes it from live
  // millis() later in the same pass. These are unsigned, so the subtraction
  // wraps to about 4.29 billion instead of going negative and clears any
  // timeout ever written. A safety timer reads the clock when it asks.
  //
  // MANUAL only, and that is a statement rather than a side effect: the
  // watchdog measures whether the operator's channel is alive, and there is an
  // operator in exactly one mode. So a rover left in AUTO that drives out of
  // range keeps going - the AEB still brakes it, but nothing brings it home.
  if (mode == Mode::Manual && !commandTimedOut &&
      millis() - lastCommandMs >= COMMAND_TIMEOUT_MS) {
    commandedLeft = 0;
    commandedRight = 0;
    commandTimedOut = true;
  }

  // A command may stand only while a source entitled to give it exists, and
  // this is checked every pass rather than at the transition that ended it.
  // LEDC holds the last duty written until something writes another, so a mode
  // that merely stops deciding leaves the rover driving on its last order:
  // STANDBY has to write the zero, not just refrain from writing. Clearing only
  // on entry would let any future writer move a parked rover between
  // transitions.
  if (!commandSourceActive()) {
    commandedLeft = 0;
    commandedRight = 0;
  }

  safeDrive(commandedLeft, commandedRight);

  // The upload steps here rather than beside the web server, because only its
  // last phase blocks and that phase needs the rover already stopped by the
  // line above.
  updateUpload();

  // Raising the access point belongs to the web module, and cloud must not
  // depend on it - web already reads cloud, and the pair would become a cycle.
  // The loop is the one place entitled to know about both, the same
  // arrangement as the calibration's drop to STANDBY.
  if (uploadNeedsAccessPoint()) {
    startAccessPoint();
    finishUpload();
  }

  // The only moment the server does anything at all - it has no task and no
  // timer of its own. That is why a synchronous server was chosen: no request
  // handler can interrupt a distance measurement, which is what makes the
  // braking timing something that can be reasoned about. Last on purpose, so it
  // serves the values this pass just took.
  //
  // With no request waiting, handleClient() calls delay(1) before returning,
  // and that is left on deliberately: it is the only yield in the loop. The
  // delay inside readEchoMedian spaces ultrasonic bursts and vanishes the day
  // that measurement goes non-blocking, so switching this off belongs in the
  // same change as an explicit yield of our own.
  if (now - lastWebHandle >= WEB_HANDLE_INTERVAL_MS) {
    lastWebHandle = now;
    handleWebClient();
  }
}
