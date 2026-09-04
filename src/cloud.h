#pragma once

// The brake log, and the one moment the rover talks to the internet.
//
// Nothing here runs while the rover is driving. Events are recorded from the
// stage the AEB already decided, and the whole network exchange happens once,
// on request, with the rover standing still - so the radio never competes with
// the brakes.

#include <Arduino.h>

// One emergency stop. The time is kept as an uptime rather than a date,
// because the rover has no clock while it drives and does not need one: what
// matters at the moment of recording is how long ago it happened. The absolute
// time is worked out once, at upload, from a single NTP answer.
//
// distanceCm keeps the meaning zero carries everywhere in this project: no echo
// came back. A stop with no reading behind it is the more interesting entry,
// not a broken one.
struct BrakeEvent {
  unsigned long atMs;
  float distanceCm;
};

// Where an upload has got to. The same shape as CalPhase, for the same reason:
// Failed is kept apart from Idle because a refusal carries a reason and nothing
// attempted does not.
enum class UploadPhase : uint8_t {
  Idle,
  Waiting,
  Syncing,
  Sending,
  Restoring,
  Done,
  Failed
};

extern UploadPhase uploadPhase;

// Why the last upload gave up. A pointer to a literal - every one of them is a
// compile-time constant that outlives any caller.
extern const char *uploadFailReason;

extern uint8_t uploadSent;

// How many events are held, and how many were overwritten because the queue was
// full. The second is reported rather than hidden: losing an entry is worth
// knowing at the time, not discovering afterwards.
uint8_t eventCount();
uint16_t eventsDropped();

// Oldest first, so index 0 is always the next one to be sent.
const BrakeEvent &eventAt(uint8_t index);

// Derived from the phase rather than stored, which is what stops a flag being
// left behind on a path that forgot to clear it.
bool uploadRunning();

// Running, or about to be. There is a short grace between answering the request
// and taking the access point down, and during it the upload has not started
// but the decision to start it has been made. Anything that must not happen
// across an upload has to be held off from here, not from uploadRunning().
bool uploadBusy();

// True while the access point is down and waiting to be raised again. The loop
// acts on it: raising the access point belongs to the web module, and this one
// must not depend on it.
bool uploadNeedsAccessPoint();
void finishUpload();

const char *uploadPhaseName(UploadPhase phase);
uint8_t uploadSecondsLeft();

// Records intent and nothing else. The loop starts the work, on a pass of its
// own choosing, exactly as it does for every request handler.
void requestUpload();

// Watches the stage the AEB decided and records the moment it becomes Stop.
void updateCloud();

// Steps the upload one loop pass at a time. Only the POST inside Sending
// blocks, and it does so with the rover stationary.
void updateUpload();
