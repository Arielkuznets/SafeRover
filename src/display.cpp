#include "display.h"

#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

#include "config.h"
#include "modes.h"
#include "pins.h"

// The last two arguments are the bus speed during a transfer and after it, both
// pinned to the same value so nothing switches the bus behind our back.
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN,
                         I2C_CLOCK_HZ, I2C_CLOCK_HZ);

// displayReady matters because the loop draws: begin() returns false when the
// framebuffer could not be allocated, and drawPixel indexes that buffer with no
// null test, so drawing after a failed allocation writes through a null
// pointer.
static bool displayReady = false;
bool apUp = false;

// What the last redraw was drawn from. Both seeded with values the real thing
// can never take, so the first check always draws.
static constexpr uint8_t DISPLAY_STATIONS_UNKNOWN = 0xFF;
static uint8_t lastDrawnStations = DISPLAY_STATIONS_UNKNOWN;
static Mode lastDrawnMode = static_cast<Mode>(0xFF);

// Every call below writes into a pixel buffer in the ESP32's own RAM. Nothing
// reaches the panel until display() pushes that whole buffer out over I2C, so
// display() is the expensive operation and the only one whose cost scales with
// the bus rather than the CPU.
static void showStatusScreen(uint8_t stations) {
  display.clearDisplay();

  constexpr uint16_t FOREGROUND = SSD1306_WHITE;

  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, FOREGROUND);

  display.setTextSize(1);
  display.setTextColor(FOREGROUND);

  // Title and mode share the top line: the panel holds three, the address and
  // the phone status take the other two, and of the three facts the mode is the
  // one worth reading from across the room.
  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP);
  display.print(SCREEN_TITLE);
  display.print(' ');
  display.print(modeName(mode));

  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP + OLED_LINE_SPACING);

  if (apUp) {
    display.print(WiFi.softAPIP());

    // Only meaningful while there is a network to be associated to, so it is
    // left off entirely rather than printed as a no.
    display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP + 2 * OLED_LINE_SPACING);
    display.print(stations > 0 ? SCREEN_PHONE_YES : SCREEN_PHONE_NO);
  } else {
    display.print(SCREEN_AP_FAILED);
  }

  display.display();
}

// Redraws only when what the screen says has changed. The address is fixed once
// setup() is over and the failure line cannot change, so two things move: the
// station count and the mode.
//
// Compared rather than flagged. A flag has to be raised by whoever changes the
// thing, so every future writer has to remember; a comparison cannot be
// forgotten. It also keeps the mode machine from having to know a screen
// exists.
void updateStatusScreen() {
  // No buffer to draw into. The rover drives perfectly well without a screen.
  if (!displayReady) {
    return;
  }

  const uint8_t stations = WiFi.softAPgetStationNum();
  const Mode current = mode;

  if (stations == lastDrawnStations && current == lastDrawnMode) {
    return;
  }

  lastDrawnStations = stations;
  lastDrawnMode = current;
  showStatusScreen(stations);
}

// The bus is opened on the named pins rather than left to the library, which
// would re-open it on the core's defaults and only work by coincidence.
//
// begin() never asks the panel anything - it ignores every I2C ACK, and its
// only failure path is the buffer allocation. false means out of RAM, not a
// missing display; an unplugged panel still returns true.
//
// Nothing is drawn here: the first screen wants to say whether the access point
// came up, which is not known yet. The loop puts it up on its first pass.
void displayBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
}
