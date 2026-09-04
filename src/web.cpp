#include "web.h"

#include <WebServer.h>
#include <WiFi.h>

#include "aeb.h"
#include "cloud.h"
#include "config.h"
#include "display.h"
#include "lka.h"
#include "modes.h"
#include "pins.h"
#include "secrets.h"
#include "sensors.h"
#include "web_page.h"

// Synchronous by design: it does no work of its own until the loop calls
// handleClient(), so nothing about it runs behind the scheduler's back.
static WebServer server(WEB_SERVER_PORT);

// Nothing here waits - softAP() returns with the interface already up and
// addressed. The result is kept because the screen is the only thing that can
// say whether the network came up, and a failure is not fatal: a rover that
// stops dead because a radio did not come up is worse than one that drives
// without it.
void startAccessPoint() {
  // AP only. WIFI_AP_STA would also raise the station interface, which has no
  // network to join and would spend its time scanning for one.
  WiFi.mode(WIFI_AP);

  apUp = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL,
                     WIFI_AP_HIDDEN, WIFI_AP_MAX_CLIENTS);
}

// PAGE_HTML is const and cannot be edited where it sits, so it is copied, the
// placeholders are substituted, and the copy is sent and released - heap for
// the length of one page load, not once per reading. Every number the page
// needs comes from a constant here, so none is written down twice.
static void handleRoot() {
  String page = PAGE_HTML;

  page.replace("%POLL_MS%", String(WEB_POLL_INTERVAL_MS));
  page.replace("%SEND_MS%", String(COMMAND_SEND_INTERVAL_MS));
  page.replace("%MIN_PERCENT%", String(DRIVE_MIN_PERCENT));
  page.replace("%START_PERCENT%", String(DRIVE_START_PERCENT));
  page.replace("%KP_MAX%", String(LKA_KP_MAX));

  server.send(200, "text/html", page);
}

// The readings, as JSON. Assembled by hand rather than with a library: a
// handful of values does not justify the dependency.
//
// A missing echo is sent as null and never as a number. The browser has to be
// able to tell no reading from zero centimetres, because a zero would describe
// an obstacle pressed against the sensor.
static void handleData() {
  String json = "{\"distance\":";

  if (echoDurationValue == 0) {
    json += "null";
  } else {
    json += String(distanceFromDuration(echoDurationValue), 1);
  }

  json += ",\"lineLeft\":";
  json += lineLeftValue;
  json += ",\"lineRight\":";
  json += lineRightValue;
  json += ",\"button\":";
  json += (lastButtonPressed ? "true" : "false");

  json += ",\"mode\":\"";
  json += modeName(mode);
  json += "\",\"calPhase\":\"";
  json += calPhaseName(calPhase);
  json += "\",\"calReason\":\"";
  json += calFailReason;
  json += "\",\"lkaActive\":";
  json += (lkaSteering() ? "true" : "false");
  json += ",\"zeroPoint\":";
  json += zeroPoint;
  json += ",\"lkaError\":";
  json += lkaError;
  json += ",\"kp\":";
  json += lkaKp;
  json += ",\"base\":";
  json += lkaBasePercent;

  // How fast the controller is actually being fed, beside what it decided: a
  // correction and the age of the reading behind it are one fact.
  json += ",\"gapAvg\":";
  json += lineAvgGapMs;
  json += ",\"gapMax\":";
  json += lineGapMaxMs;

  // null rather than a number when the window held no samples, so an idle
  // controller cannot be mistaken for a quiet signal.
  json += ",\"errMin\":";
  json += (lkaErrorSeen ? String(lkaErrorMin) : String("null"));
  json += ",\"errMax\":";
  json += (lkaErrorSeen ? String(lkaErrorMax) : String("null"));

  // Two AEB fields answering different questions: aeb is what the layer is
  // doing now, aebPeak the worst it did since the last poll - the only way a
  // 500 ms page can show a 50 ms braking event at all.
  json += ",\"aeb\":\"";
  json += aebStageName(aebStage);
  json += "\",\"aebPeak\":\"";
  json += aebStageName(aebPeakStage);
  json += "\"";

  // The brake log, oldest first. Ages rather than dates, because the rover has
  // no clock until an upload resolves one - the page turns them into "so long
  // ago" and the absolute time is only ever produced at Adafruit's end.
  json += ",\"events\":[";

  for (uint8_t i = 0; i < eventCount(); i++) {
    const BrakeEvent &event = eventAt(i);

    if (i > 0) {
      json += ',';
    }

    json += "{\"age\":";
    json += (millis() - event.atMs);
    json += ",\"cm\":";
    json += (event.distanceCm == 0.0f) ? String("null")
                                       : String(event.distanceCm, 1);
    json += '}';
  }

  json += "],\"dropped\":";
  json += eventsDropped();

  json += ",\"upload\":\"";
  json += uploadPhaseName(uploadPhase);
  json += "\",\"uploadSent\":";
  json += uploadSent;
  json += ",\"uploadReason\":\"";
  json += uploadFailReason;
  json += "\"";

  // The one place a handler clears state rather than only reporting it. Reset
  // on read is what makes these windows mean "since you last looked" rather
  // than "since boot", and it is safe because nothing in the control path reads
  // them.
  lkaErrorSeen = false;
  aebPeakStage = AebStage::Clear;

  json += "}";

  server.send(200, "application/json", json);
}

// Turns one command from the page into the two side values the motors take.
//
// Everything is checked rather than trusted. The page cannot produce a speed
// outside the slider's range, so one arriving means a stale cached page or a
// request made without the page at all, which anyone on this network can do.
//
// Out of range is rejected rather than clamped. Clamping hides the case that
// matters: String::toInt() answers zero for "abc" exactly as for "0", so it
// would quietly turn nonsense into a crawl.
static void handleDrive() {
  const String dir = server.arg("dir");

  int16_t left = 0;
  int16_t right = 0;

  // Stop is answered first, without reading the speed at all: refusing one
  // because some other field was malformed would leave the rover driving on a
  // validation error.
  if (dir != "stop") {
    const String speedArg = server.arg("speed");
    const long percent = speedArg.toInt();

    // A missing argument needs no test of its own: server.arg() answers with an
    // empty string, toInt() reads that as zero, and zero is below the minimum.
    // Read into a long and range-checked before it is narrowed - narrowing
    // first could fold an absurd value back inside the valid range.
    if (percent < DRIVE_MIN_PERCENT || percent > 100) {
      server.send(400, "text/plain", "bad speed");
      return;
    }

    const uint8_t percentValue = static_cast<uint8_t>(percent);
    const int16_t magnitude =
        static_cast<int16_t>(dutyFromPercent(percentValue));

    // The one place in this project that knows what "left" means. Below it
    // there are two numbers and nothing else: there is no servo, and a turn is
    // a difference between the sides.
    if (dir == "fwd") {
      left = magnitude;
      right = magnitude;
    } else if (dir == "back") {
      left = -magnitude;
      right = -magnitude;
    } else if (dir == "left") {
      left = -magnitude;
      right = magnitude;
    } else if (dir == "right") {
      left = magnitude;
      right = -magnitude;
    } else {
      server.send(400, "text/plain", "bad dir");
      return;
    }
  }

  // A deliberate press is the operator taking the wheel, from any mode. Real
  // lane keeping disengages the same way and for the same reason: two things
  // steering one vehicle is worse than either steering it alone.
  //
  // Only a marked request counts. The page sends a heartbeat on a timer whether
  // or not anything is touched, so treating that as an instruction would drag
  // the rover out of STANDBY the moment a phone connected, and out of AUTO
  // several times a second after that.
  //
  // By origin, never by content. That is also why the STOP button still stops
  // an autonomous rover: it is a press, so it is marked, so it takes the wheel
  // - even though what it asks for is a stop.
  if (server.hasArg("manual")) {
    enterMode(Mode::Manual);
  }

  // Only MANUAL accepts a driving command, and this holds independently of
  // where handleClient sits in the loop. An unmarked heartbeat arrives about
  // eight times a second: in AUTO it would overwrite the controller's steering,
  // in STANDBY it would move a rover nobody asked to move. The condition
  // restates what the block above settled rather than inheriting it, so a later
  // edit there cannot silently open this one.
  if (mode == Mode::Manual) {
    commandedLeft = left;
    commandedRight = right;
  }

  // Outside the guard on purpose: a heartbeat is exactly the evidence the
  // watchdog exists to collect, because the timer asks whether the channel is
  // alive and not whether the operator asked for anything. Only a command that
  // validated gets this far - a rejected request proves something is sending,
  // not that an operator is there.
  lastCommandMs = millis();
  commandTimedOut = false;

  server.send(200, "text/plain", "ok");
}

// Sets Kp and the base speed from the dashboard. Its own endpoint rather than
// more fields on /drive: that says where to go now, this says how the
// controller should behave from now on, and mixing them would mean a tuning
// change arriving through the path the watchdog watches. Out of range is
// refused rather than clamped, the same rule /drive follows.
static void handleTune() {
  bool changed = false;

  if (server.hasArg("kp")) {
    const long kp = server.arg("kp").toInt();

    if (kp < 0 || kp > LKA_KP_MAX) {
      server.send(400, "text/plain", "bad kp");
      return;
    }

    lkaKp = static_cast<uint8_t>(kp);
    changed = true;
  }

  if (server.hasArg("base")) {
    const long base = server.arg("base").toInt();

    // Zero is allowed through as the one exception, and it is a bench setting:
    // with no base speed the two sides get nothing but the correction, in
    // opposite directions, so a rover held off the ground shows the
    // controller's output and nothing else. It only shows anything at a large
    // Kp, since the correction can never exceed Kp and an ordinary one asks for
    // less duty than starts a motor.
    if (base != 0 && (base < DRIVE_MIN_PERCENT || base > 100)) {
      server.send(400, "text/plain", "bad base");
      return;
    }

    lkaBasePercent = static_cast<uint8_t>(base);
    changed = true;
  }

  if (!changed) {
    server.send(400, "text/plain", "nothing to set");
    return;
  }

  server.send(200, "text/plain", "ok");
}

// Takes a calibration without driving on it - the only way to measure a zero
// point and read the number rather than leave on it, which the button lost with
// its long press.
//
// STANDBY only, and it does not change the mode. lkaSteering() asks for AUTO as
// well, so nothing here can put a wheel in motion. Entering AUTO later starts
// its own calibration and overwrites whatever this measured.
static void handleCalibrate() {
  if (mode != Mode::Standby) {
    server.send(409, "text/plain", "standby only");
    return;
  }

  startCalibration();
  server.send(200, "text/plain", "ok");
}

// Starts an upload. Records intent and answers immediately - the loop takes the
// access point down a moment later, so this reply has to be on its way out
// before the network carrying it disappears.
//
// STANDBY only, the same guard and the same reason as the calibration: this
// drops the network the operator is driving over, and it must not be reachable
// from a moving rover.
static void handleUpload() {
  if (mode != Mode::Standby) {
    server.send(409, "text/plain", "standby only");
    return;
  }

  if (uploadRunning()) {
    server.send(409, "text/plain", "already running");
    return;
  }

  if (eventCount() == 0) {
    server.send(409, "text/plain", "nothing to send");
    return;
  }

  requestUpload();
  server.send(200, "text/plain", "ok");
}

// Wrapped rather than exposing the server object, so the loop schedules the
// module instead of reaching inside it.
void handleWebClient() { server.handleClient(); }

// on() fills in a routing table rather than running anything now. The method is
// given explicitly rather than left as HTTP_ANY, because a request that moves a
// motor must not be a GET. Nothing here can report failure: begin() returns
// void and the listening flag has no accessor, so the first request the browser
// makes is the proof.
void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/drive", HTTP_POST, handleDrive);
  server.on("/tune", HTTP_POST, handleTune);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/upload", HTTP_POST, handleUpload);
  server.begin();
}
