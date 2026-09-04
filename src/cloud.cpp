#include "cloud.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

#include "aeb.h"
#include "config.h"
#include "secrets.h"
#include "sensors.h"

// A ring buffer, not a list that grows. The size is fixed at compile time and
// the memory is allocated once, so there is no allocation to fail and no
// fragmentation to accumulate on a board that runs for hours.
static BrakeEvent events[EVENT_QUEUE_SIZE];
static uint8_t eventHead = 0;
static uint8_t eventHeld = 0;
static uint16_t eventDropped = 0;

// What the AEB stage was the last time this looked. The recording is driven by
// the difference between the two, so nothing has to remember to announce
// itself - the same arrangement the screen uses against the mode.
static AebStage lastSeenStage = AebStage::Clear;

UploadPhase uploadPhase = UploadPhase::Idle;
const char *uploadFailReason = "";
uint8_t uploadSent = 0;

static bool uploadRequested = false;
static unsigned long uploadRequestMs = 0;
static unsigned long phaseStartMs = 0;

// When the last association attempt was issued, and whether any of them came
// back refused. Both belong to the waiting phase alone and are reset when it
// starts.
static unsigned long lastAttemptMs = 0;
static bool authRefused = false;

// The single NTP answer, and the uptime at which it arrived. Every event's
// absolute time is worked out from this pair: the rover keeps a stopwatch, not
// a calendar, and the two are reconciled once.
static time_t ntpEpoch = 0;
static unsigned long ntpAtMs = 0;

uint8_t eventCount() { return eventHeld; }
uint16_t eventsDropped() { return eventDropped; }

// The oldest entry sits eventHeld places behind the write cursor. Adding the
// size before the modulo keeps the arithmetic positive when the cursor has
// wrapped past the start of the array.
const BrakeEvent &eventAt(uint8_t index) {
  const int slot =
      (static_cast<int>(eventHead) - static_cast<int>(eventHeld) + index +
       EVENT_QUEUE_SIZE) %
      EVENT_QUEUE_SIZE;

  return events[slot];
}

bool uploadRunning() {
  return uploadPhase == UploadPhase::Waiting ||
         uploadPhase == UploadPhase::Syncing ||
         uploadPhase == UploadPhase::Sending ||
         uploadPhase == UploadPhase::Restoring;
}

bool uploadBusy() { return uploadRequested || uploadRunning(); }

bool uploadNeedsAccessPoint() {
  return uploadPhase == UploadPhase::Restoring;
}

const char *uploadPhaseName(UploadPhase phase) {
  switch (phase) {
  case UploadPhase::Waiting:
    return "waiting";
  case UploadPhase::Syncing:
    return "syncing";
  case UploadPhase::Sending:
    return "sending";
  case UploadPhase::Restoring:
    return "restoring";
  case UploadPhase::Done:
    return "done";
  case UploadPhase::Failed:
    return "failed";
  default:
    return "idle";
  }
}

// Whole seconds, so the screen redraws once a second rather than on every pass.
uint8_t uploadSecondsLeft() {
  if (uploadPhase != UploadPhase::Waiting) {
    return 0;
  }

  const unsigned long elapsed = millis() - phaseStartMs;

  if (elapsed >= UPLOAD_WAIT_MS) {
    return 0;
  }

  return static_cast<uint8_t>((UPLOAD_WAIT_MS - elapsed) / 1000UL);
}

// --- the queue ---

static void recordEvent(float distanceCm) {
  events[eventHead].atMs = millis();
  events[eventHead].distanceCm = distanceCm;

  eventHead = static_cast<uint8_t>((eventHead + 1) % EVENT_QUEUE_SIZE);

  if (eventHeld < EVENT_QUEUE_SIZE) {
    eventHeld++;
  } else {
    // The write cursor has just run over the oldest entry. Counting it is what
    // makes the loss visible at the time instead of a gap noticed later.
    eventDropped++;
  }
}

// Dropping the oldest is a subtraction: the read position is derived from the
// count, so lowering it moves the window forward without touching the array.
static void dropOldest() {
  if (eventHeld > 0) {
    eventHeld--;
  }
}

// Records the transition into Stop, not the state of being stopped. Sitting at
// Stop for three seconds is one event; without the comparison it would be
// sixty, and the queue would fill from a single obstacle.
void updateCloud() {
  if (aebStage == lastSeenStage) {
    return;
  }

  lastSeenStage = aebStage;

  if (aebStage != AebStage::Stop) {
    return;
  }

  // The same reading the AEB acted on this pass. Taken from the raw pulse
  // rather than from the AEB's own copy of the distance, which is private to
  // that module and should stay that way.
  const float distanceCm = (echoDurationValue == 0)
                               ? 0.0f
                               : distanceFromDuration(echoDurationValue);

  recordEvent(distanceCm);
}

// --- the upload ---

// The single exit from the network phases. Taking the station down here, rather
// than after the access point is back, means the radio is only ever
// reconfigured in one direction at a time.
//
// Restoring rather than a terminal state, because the access point has to come
// back however this ended.
static void endStation() {
  WiFi.disconnect(true);
  uploadPhase = UploadPhase::Restoring;
}

static void failUpload(const char *reason) {
  uploadFailReason = reason;
  endStation();
}

void requestUpload() {
  if (uploadRunning()) {
    return;
  }

  uploadRequested = true;
  uploadRequestMs = millis();
}

static void beginUpload() {
  uploadSent = 0;
  uploadFailReason = "";

  // The access point goes down first. One radio cannot serve a network and
  // join another on a different channel, and taking it down deliberately is
  // better than having it dragged onto the router's channel with a phone
  // attached to it.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

  uploadPhase = UploadPhase::Waiting;
  phaseStartMs = millis();
  lastAttemptMs = millis();
  authRefused = false;
}

// Polled rather than waited on. WiFi.begin() returns immediately and the
// association finishes in the background, which is what lets the loop keep
// running for the whole minute this window can last.
static void stepWaiting() {
  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    // UTC, no offset. SNTP then answers in its own time and time() starts
    // returning something real; there is nothing to wait on here either.
    configTime(0, 0, NTP_SERVER);

    uploadPhase = UploadPhase::Syncing;
    phaseStartMs = millis();
    return;
  }

  // Remembered rather than acted on. A refused passphrase and an absent network
  // need different fixes, so they are reported apart - but one reading of
  // WL_CONNECT_FAILED is not enough to give up on, because the retry below can
  // still succeed once the network appears.
  if (status == WL_CONNECT_FAILED) {
    authRefused = true;
  }

  // The whole point of this phase is that the network is NOT there when it
  // starts: the operator is walking to the phone to turn the hotspot on. One
  // WiFi.begin() at the top would leave the rest of the window depending on the
  // core's own reconnect behaviour, which is not something this should rest on.
  // So the attempt is re-issued on a cadence of its own.
  //
  // Only from a settled state. Re-issuing while an association is in progress
  // would abort an attempt that might have been about to succeed.
  const bool settled =
      (status == WL_NO_SSID_AVAIL || status == WL_DISCONNECTED ||
       status == WL_CONNECT_FAILED || status == WL_IDLE_STATUS);

  if (settled && millis() - lastAttemptMs >= WIFI_RETRY_MS) {
    lastAttemptMs = millis();
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
  }

  if (millis() - phaseStartMs >= UPLOAD_WAIT_MS) {
    // The distinction only becomes reportable once the window is over: until
    // then, a network that was refused and a network that is not up yet look
    // the same from here.
    failUpload(authRefused ? "wifi auth" : "no wifi");
  }
}

static void stepSyncing() {
  const time_t now = time(nullptr);

  if (now >= NTP_EPOCH_VALID) {
    ntpEpoch = now;
    ntpAtMs = millis();

    uploadPhase = UploadPhase::Sending;
    phaseStartMs = millis();
    return;
  }

  // Associated with no route out looks exactly like this, which is why the
  // reason says nothing about the link.
  if (millis() - phaseStartMs >= UPLOAD_SYNC_MS) {
    failUpload("no time");
  }
}

// Turns an uptime into a date, using the one NTP answer as the anchor.
static time_t epochOf(const BrakeEvent &event) {
  const unsigned long ageMs = ntpAtMs - event.atMs;

  return ntpEpoch - static_cast<time_t>(ageMs / 1000UL);
}

static int postEvent(const BrakeEvent &event) {
  const time_t stamp = epochOf(event);

  struct tm utc;
  gmtime_r(&stamp, &utc);

  char isoTime[24];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", &utc);

  String body = "{\"value\":\"";
  body += String(event.distanceCm, 1);
  body += "\",\"created_at\":\"";
  body += isoTime;
  body += "\"}";

  String url = "https://";
  url += AIO_HOST;
  url += "/api/v2/";
  url += AIO_USERNAME;
  url += "/feeds/";
  url += AIO_FEED;
  url += "/data";

  // setInsecure() encrypts the exchange but does not check who is on the other
  // end of it. That is a deliberate trade: it defeats a passive listener on the
  // network, not an active impersonator, and the alternative is carrying a
  // certificate bundle that expires. The network here is a phone held by the
  // operator.
  NetworkClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    return -1;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-AIO-Key", AIO_KEY);

  const int code = http.POST(body);
  http.end();

  return code;
}

// Each code names the thing to go and check, because with no serial output this
// string on the screen is the whole of the diagnosis.
static const char *httpReason(int code) {
  switch (code) {
  case 401:
  case 403:
    return "http 401 key";
  case 404:
    return "http 404 feed";
  case 429:
    return "http 429 rate";
  default:
    return (code < 0) ? "no reply" : "http error";
  }
}

// One event per pass. The POST blocks, so draining the whole queue in a single
// pass would hold the loop for as long as the queue is deep; this way the
// scheduler breathes between them and the rover is never deaf for more than one
// request at a time.
static void stepSending() {
  if (eventHeld == 0) {
    endStation();
    return;
  }

  const int code = postEvent(eventAt(0));

  // Only a confirmed acceptance retires an event. Anything else leaves the
  // queue as it was, so a failed upload loses nothing and can simply be tried
  // again.
  if (code == 200 || code == 201) {
    dropOldest();
    uploadSent++;
    return;
  }

  // No count of failures is kept. Sending stops at the first one, so it
  // could only ever be one - and the phase already says that, with the
  // reason beside it.
  failUpload(httpReason(code));
}

// Called by the loop once the access point is back up. The station is already
// down by then, so all that is left is to say how it went.
void finishUpload() {
  phaseStartMs = millis();

  uploadPhase = (uploadFailReason[0] == '\0') ? UploadPhase::Done
                                              : UploadPhase::Failed;
}

void updateUpload() {
  switch (uploadPhase) {
  case UploadPhase::Done:
  case UploadPhase::Failed:
    // A result holds the panel long enough to be read and then releases it, so
    // the screen is not still reporting an upload while the rover drives again.
    // The counts behind it are left alone.
    if (millis() - phaseStartMs >= UPLOAD_RESULT_MS) {
      uploadPhase = UploadPhase::Idle;
    }

    // Falls through on purpose. A fresh request is accepted from any of the
    // three states that are not running, and must not have to wait out the
    // timeout above first.
    [[fallthrough]];

  case UploadPhase::Idle:
    // The delay is not cosmetic: the reply to the request travels over the
    // access point this is about to take down.
    if (uploadRequested &&
        millis() - uploadRequestMs >= UPLOAD_START_DELAY_MS) {
      uploadRequested = false;
      beginUpload();
    }
    return;

  case UploadPhase::Waiting:
    stepWaiting();
    return;

  case UploadPhase::Syncing:
    stepSyncing();
    return;

  case UploadPhase::Sending:
    stepSending();
    return;

  default:
    // Restoring waits for the loop; Done and Failed hold their result until the
    // next request replaces it.
    return;
  }
}
