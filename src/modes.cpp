#include "modes.h"

#include "config.h"
#include "lka.h"
#include "pins.h"

// Standby at boot by decision, not by a zero happening to mean stopped.
Mode mode = Mode::Standby;

// Held here rather than acted on where they arrive. A request handler must not
// be the thing that writes to motors; the loop is, once per pass, at a moment
// of its own choosing. That leaves a single writer at a known point, which is
// where the AEB intercepts.
//
// commandTimedOut starts true, meaning no command has ever arrived rather than
// one arrived just now - so the rover is quiet after a reset by decision rather
// than by these values happening to be zero.
int16_t commandedLeft = 0;
int16_t commandedRight = 0;
unsigned long lastCommandMs = 0;
bool commandTimedOut = true;

const char *modeName(Mode m) {
  switch (m) {
  case Mode::Manual:
    return "MANUAL";
  case Mode::Auto:
    return "AUTO";
  default:
    return "STANDBY";
  }
}

// The button's whole vocabulary. The long press that used to start a
// calibration is gone: the calibration belongs to the AUTO transition, and a
// hand-started one could not survive to be driven on.
Mode nextMode(Mode current) {
  switch (current) {
  case Mode::Standby:
    return Mode::Manual;
  case Mode::Manual:
    return Mode::Auto;
  default:
    return Mode::Standby;
  }
}

// Derived rather than stored, which is what let a bool called lkaActive be
// deleted: AUTO says the operator asked for it, Ready says there is a zero
// point to do it with, and neither can be left behind by a path that forgot to
// clear a flag.
//
// False for the whole of the entry calibration - the phase is Settling or
// Sampling then, not Ready - so the rover stands still while it measures.
bool lkaSteering() {
  return mode == Mode::Auto && calPhase == CalPhase::Ready;
}

// Whether anyone is entitled to be commanding the motors at all.
//
// A command may stand only while the source that issued it exists. In STANDBY
// there is none; in AUTO there is none until the calibration finishes, and none
// again once the lane keeping gives up. Zeroing on this one condition closes
// three holes that each used to depend on something else remembering: a
// heartbeat driving a parked rover, a held button driving the rover through its
// own calibration, and a stale command left standing after the lane keeping
// stopped.
bool commandSourceActive() {
  return mode == Mode::Manual || lkaSteering();
}

// The only writer of mode. Exit work and entry work both happen here, so a
// transition cannot half-happen somewhere else - which is the whole reason this
// exists rather than more flags.
void enterMode(Mode next) {
  if (next == mode) {
    return;
  }

  // A measurement in progress belongs to the mode it was started in: readings
  // taken while the rover is driven somewhere else describe the journey.
  //
  // Leaving AUTO also throws away a finished zero point. The rover cannot know
  // how far it was driven or how the light moved since, and a fresh measurement
  // costs 800 ms. The measurement behind that decision is at the CalPhase
  // declaration.
  //
  // A refusal survives on purpose - the reason is the whole of what Failed
  // carries and the operator has not read it yet. Nothing can be driven on it
  // either way, because lkaSteering() asks for Ready.
  if (calibrationRunning()) {
    abortCalibration();
  } else if (mode == Mode::Auto && calPhase != CalPhase::Failed) {
    calPhase = CalPhase::Idle;
  }

  mode = next;

  // commandSourceActive() would zero these next pass anyway. Doing it here
  // means no mode ever begins holding an order given to a different one.
  commandedLeft = 0;
  commandedRight = 0;

  // No operator command has arrived in this session yet, and recording that is
  // more honest than carrying a timestamp from one that has ended. Without it
  // the watchdog fires on the first pass and reports a timeout nobody caused.
  if (next == Mode::Manual) {
    commandTimedOut = true;
  }

  // The operator asked for it to drive itself; measuring is the first half.
  if (next == Mode::Auto) {
    startCalibration();
  }

  // Lit for anything that can move - the one question a single lamp answers
  // well.
  digitalWrite(PIN_STATUS_LED, next == Mode::Standby ? LOW : HIGH);
}

// The edge is what acts, rather than every pass a finger is down: one press has
// to mean one mode change.
bool lastButtonPressed = false;
static unsigned long lastButtonChange = 0;

// Acted on the press rather than the release, because there is no length to
// measure any more and a control that answers on the way down feels immediate.
// The debounce window is what keeps the contacts bouncing for a few
// milliseconds from reading as a burst of separate edges.
//
// Every case the old three-rule button handled falls out of this on its own. A
// press during the entry calibration takes AUTO to STANDBY, and leaving AUTO
// aborts a running calibration. A press while the rover is steering does the
// same and stops it. Nothing needed a special branch.
void updateModeButton(unsigned long now) {
  const bool pressed = (digitalRead(PIN_MODE_BUTTON) == LOW);

  if (pressed == lastButtonPressed ||
      now - lastButtonChange < BUTTON_DEBOUNCE_MS) {
    return;
  }

  lastButtonChange = now;
  lastButtonPressed = pressed;

  if (pressed) {
    enterMode(nextMode(mode));
  }
}

// The button and the lamp are the two ends of the same control, so they are
// raised together. Dark, because the rover boots into STANDBY.
//
// INPUT_PULLUP switches on a resistor inside the ESP32 that holds the pin at
// 3V3 while the button is open; the other leg goes to GND, so a press reads
// LOW. That is why no external resistor is wired.
void modesBegin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);
}
