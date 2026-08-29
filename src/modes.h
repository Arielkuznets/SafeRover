#pragma once

// Who is driving. Exactly one source at a time, which is the whole point: the
// question "what happens when the phone and the lane keeping both command"
// stops having an answer because it stops being reachable.
//
// The AEB is deliberately not a mode. It is a layer - it runs in all three,
// inside safeDrive, and nothing here can switch it off.

#include <Arduino.h>

enum class Mode : uint8_t { Standby, Manual, Auto };

// enterMode() is the only writer. Read it anywhere; set it nowhere else.
extern Mode mode;

// What the current source has asked for, in the form drive() takes: sign is
// direction, magnitude is duty. Held here because the mode governs who may
// write them.
extern int16_t commandedLeft;
extern int16_t commandedRight;
extern unsigned long lastCommandMs;
extern bool commandTimedOut;

// The debounced state of the mode button. Exposed only so the dashboard can
// report it; nothing outside this module decides anything from it.
extern bool lastButtonPressed;

const char *modeName(Mode m);
Mode nextMode(Mode current);

bool lkaSteering();
bool commandSourceActive();

void enterMode(Mode next);

// Takes the loop's timestamp rather than reading the clock itself, so it is
// scheduled by the same `now` as everything else in the pass.
void updateModeButton(unsigned long now);

void modesBegin();
