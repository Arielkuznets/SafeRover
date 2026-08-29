#pragma once

// The access point and the dashboard it serves.
//
// Handlers record intent and never touch a motor. A handler that drove would
// let a browser decide when pulseIn fires; the loop remains the single writer,
// at a moment of its own choosing.

void startAccessPoint();
void startWebServer();
void handleWebClient();
