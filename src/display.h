#pragma once

// The on-vehicle status screen.
//
// Deliberately not live telemetry: a 0.91" panel is unreadable while the rover
// moves, and the dashboard already does that job. This says what the rover is
// and how to reach it.

#include <Arduino.h>

// Whether the access point came up. Written by the web module, read here - the
// screen is the only thing that can say so once setup() has returned.
extern bool apUp;

void updateStatusScreen();
void displayBegin();
