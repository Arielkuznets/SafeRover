// SafeRover - the scheduler.
//
// Everything else lives in a module. What is left here is the non-blocking
// millis() loop that decides when each of them runs, and the single write to
// the motors that every driving decision has to pass through.
//
// No delay() past setup(). Each task carries its own timestamp and interval,
// because a delay is a period of blindness: the sensors stop being read and the
// safety layer stops being asked.

#include <Arduino.h>

#include "aeb.h"
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
  // First, before anything else: the buzzer sounds while its pin is undriven,
  // so every instruction ahead of this one is spent making noise.
  //
  // The order cannot be inverted. A write to a pin that has not been configured
  // is dropped rather than queued - the peripheral manager does not recognise
  // the pin and gpio_set_level is never reached. Setting the latch ahead of
  // pinMode is an AVR habit, and here it silently did nothing.
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
  // hand before every upload, because esptool holds the board in ROM download
  // mode where no firmware of ours runs to silence the pin. Anything unplugged
  // by hand can be left unplugged, and the AEB would then warn in silence with
  // nothing wrong in the code to find. This beep says the wire went back.
  //
  // delay() is allowed here: the rule against it applies to the loop.
  digitalWrite(PIN_BUZZER, BUZZER_SOUND);
  delay(BUZZER_TEST_MS);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);
}

void loop() {
  const unsigned long now = millis();

  // First on purpose: on the rare pass where it does redraw it blocks for tens
  // of milliseconds, and the distance reading taken afterwards is then the
  // fresher one rather than the one left waiting behind it.
  if (now - lastDisplayCheck >= DISPLAY_CHECK_INTERVAL_MS) {
    lastDisplayCheck = now;
    updateStatusScreen();
  }

  // The button is the mode selector's input, so it is read where the mode
  // lives.
  updateModeButton(now);

  // Data collection only: read and accumulate, no thresholds and no decisions.
  if (now - lastLineRead >= LINE_READ_INTERVAL_MS) {
    // Measured before the timestamp is overwritten, because the useful number
    // is the interval that actually happened rather than the one asked for. The
    // scheduler re-arms from now rather than from the previous deadline, so a
    // late pass stays late instead of being caught up.
    //
    // The first read has nothing to measure against - lastLineRead is still
    // zero and now is seconds past boot, so the whole of setup() would be
    // recorded as one gap and sit in the maximum for the rest of the run.
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
    // check reads: a rover nudged while being measured shows here as a spread
    // of hundreds rather than tens.
    lineLeftMin = min(lineLeftMin, lineLeftValue);
    lineLeftMax = max(lineLeftMax, lineLeftValue);
    lineRightMin = min(lineRightMin, lineRightValue);
    lineRightMax = max(lineRightMax, lineRightValue);

    // Inside the same block as the read, so the window counts readings and not
    // loop passes.
    accumulateCalibrationSample();
  }

  // The first window is discarded: it starts at the zero the timestamp was
  // initialised to and spans the whole of setup(), so its average would
  // describe the boot rather than the loop.
  if (lastLineSummary == 0) {
    lastLineSummary = now;
    lineReadsInWindow = 0;
  } else if (now - lastLineSummary >= LINE_SUMMARY_INTERVAL_MS) {
    const unsigned long elapsed = now - lastLineSummary;
    const uint16_t reads = lineReadsInWindow;

    lastLineSummary = now;
    lineReadsInWindow = 0;

    // A window with no reads has no average, and working one out would divide
    // by zero.
    if (reads > 0) {
      lineAvgGapMs = elapsed / reads;
    }
  }

  // Median-filtered not to smooth a wobble - the raw signal is steady to within
  // a tenth of a percent against a fixed target - but for the occasional sample
  // that comes back badly wrong, which is what a moving vehicle produces.
  if (now - lastDistanceRead >= DISTANCE_READ_INTERVAL_MS) {
    lastDistanceRead = now;

    // Stored as the raw pulse, not as a distance, and zero keeps the meaning it
    // carries everywhere in this project: no echo came back. Converting it
    // would produce 0.0 cm, which reads exactly like an obstacle pressed
    // against the sensor - the most dangerous misreading available to a braking
    // layer.
    echoDurationValue = readEchoMedian();

    // Inside the same block as the read, so the stage is reconsidered only when
    // there is something new to reconsider it from.
    updateAebStage();
    confirmWarnStage();
  }

  // Advances a phase and nothing else: it writes no command and moves no wheel.
  updateCalibration();

  // These two decisions used to live inside the modules. updateLka called
  // enterMode, and enterMode calls startCalibration, so the controller and the
  // state machine each needed the other. The loop is the one place allowed to
  // know about both.
  //
  // A refused calibration is not a state to drive in, and the drop to STANDBY
  // is what makes the rover safe while the reason is read off the dashboard. It
  // runs once: enterMode returns immediately when the mode is already right.
  if (mode == Mode::Auto && calPhase == CalPhase::Failed) {
    enterMode(Mode::Standby);
  }

  // After the calibration, so a measurement that just finished steers on the
  // same pass it completed. Before safeDrive, so what it writes is judged by
  // the safety layer like any other command.
  if (lkaSteering()) {
    if (lkaOffLane()) {
      // Demonstrably off the track, so the zero point it was steering by
      // describes somewhere the rover no longer is - and leaving AUTO discards
      // it. It sounds, because an operator watching the floor has to be told.
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
  // line read `now` for a day and the rover was unusable. now is taken once at
  // the top of the pass and every other schedule compares it against a stamp
  // written as `= now`, which can therefore never be newer. lastCommandMs is
  // the exception: it is written from live millis() inside a request handler,
  // which runs later in the pass. These are unsigned, so now - lastCommandMs
  // with lastCommandMs the larger wraps to about 4.29 billion instead of going
  // negative, clearing any timeout ever written. A safety timer reads the clock
  // when it asks, not what the clock said earlier.
  //
  // MANUAL only, and that is a statement rather than a side effect: the
  // watchdog measures whether the operator's channel is alive, and there is an
  // operator in exactly one mode. The consequence is deliberate - a rover left
  // in AUTO that drives out of Wi-Fi range keeps going. The AEB still brakes it
  // for obstacles, but nothing brings it home.
  if (mode == Mode::Manual && !commandTimedOut &&
      millis() - lastCommandMs >= COMMAND_TIMEOUT_MS) {
    commandedLeft = 0;
    commandedRight = 0;
    commandTimedOut = true;
  }

  // A command may stand only while a source entitled to give it exists, and
  // this is checked every pass rather than at the transition that ended it.
  // LEDC holds the last duty written until something writes another, so a mode
  // that merely stops deciding leaves the rover driving on its last order.
  // STANDBY has to write the zero, not just refrain from writing.
  //
  // Every pass, for the reason safeDrive runs every pass: a state is not a
  // moment, and clearing only on entry would let any future writer move a
  // parked rover between transitions.
  if (!commandSourceActive()) {
    commandedLeft = 0;
    commandedRight = 0;
  }

  safeDrive(commandedLeft, commandedRight);

  // The only moment the server does anything at all - it has no task and no
  // timer of its own. That is why a synchronous server was chosen: the
  // scheduler stays in charge and no request handler can interrupt a distance
  // measurement, which is what makes the braking timing something that can be
  // reasoned about.
  //
  // Last on purpose, so it serves the values this pass just took.
  //
  // With no request waiting, handleClient() calls delay(1) before returning,
  // and that is left on deliberately. Switching it off makes yielding this
  // loop's job, and the only yield left would be the delay inside
  // readEchoMedian - which exists to space ultrasonic bursts, not to feed the
  // scheduler, and vanishes the day that measurement goes non-blocking. Turning
  // it off belongs in the same change as an explicit yield of our own.
  if (now - lastWebHandle >= WEB_HANDLE_INTERVAL_MS) {
    lastWebHandle = now;
    handleWebClient();
  }
}
