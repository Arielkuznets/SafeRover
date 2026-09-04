#include "display.h"

#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

#include "cloud.h"
#include "config.h"
#include "modes.h"
#include "pins.h"

// The last two arguments are the bus speed during a transfer and after it, both
// pinned to the same value so nothing switches the bus behind our back.
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN,
                         I2C_CLOCK_HZ, I2C_CLOCK_HZ);

// displayReady matters because the loop draws: begin() returns false when the
// framebuffer could not be allocated, and drawPixel indexes that buffer with no
// null test.
static bool displayReady = false;
bool apUp = false;

// What the last redraw was drawn from. Both seeded with values the real thing
// can never take, so the first check always draws.
static constexpr uint8_t DISPLAY_STATIONS_UNKNOWN = 0xFF;
static uint8_t lastDrawnStations = DISPLAY_STATIONS_UNKNOWN;
static Mode lastDrawnMode = static_cast<Mode>(0xFF);

// The upload has its own screen, and its own two things that move. The seconds
// are counted in whole units so the countdown redraws once a second rather than
// on every pass.
static UploadPhase lastDrawnPhase = static_cast<UploadPhase>(0xFF);
static uint8_t lastDrawnDetail = 0xFF;

// What the rover has to say while the dashboard cannot be reached. During an
// upload the access point is down by design, so this panel is the only channel
// left - which is the whole reason a screen worth almost nothing during normal
// driving earns its place on the vehicle.
static void showUploadScreen() {
  display.clearDisplay();

  constexpr uint16_t FOREGROUND = SSD1306_WHITE;

  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, FOREGROUND);
  display.setTextSize(1);
  display.setTextColor(FOREGROUND);

  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP);
  display.print(SCREEN_UPLOAD_TITLE);

  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP + OLED_LINE_SPACING);

  switch (uploadPhase) {
  case UploadPhase::Waiting:
    display.print(SCREEN_UPLOAD_WAIT);
    display.print(uploadSecondsLeft());
    display.print('s');
    break;
  case UploadPhase::Syncing:
    display.print(SCREEN_UPLOAD_TIME);
    break;
  case UploadPhase::Sending:
    display.print(SCREEN_UPLOAD_SEND);
    display.print(uploadSent);
    display.print('/');
    display.print(uploadSent + eventCount());
    break;
  case UploadPhase::Restoring:
    display.print(SCREEN_UPLOAD_BACK);
    break;
  case UploadPhase::Failed:
    display.print(uploadFailReason);
    break;
  default:
    display.print(SCREEN_UPLOAD_SENT);
    display.print(uploadSent);
    break;
  }

  display.display();
}

// Every call below writes into a pixel buffer in the ESP32's own RAM. Nothing
// reaches the panel until display() pushes that buffer out over I2C, so that is
// the expensive call and the only one whose cost scales with the bus.
static void showStatusScreen(uint8_t stations) {
  display.clearDisplay();

  constexpr uint16_t FOREGROUND = SSD1306_WHITE;

  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, FOREGROUND);

  display.setTextSize(1);
  display.setTextColor(FOREGROUND);

  // Title and mode share the top line; the address and phone status take the
  // other two.
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

// Redraws only when what the screen says has changed - the address is fixed
// once setup() is over, so two things move: the station count and the mode.
//
// Compared rather than flagged. A flag has to be raised by whoever changes the
// thing, so every future writer has to remember; a comparison cannot be
// forgotten, and the mode machine never has to know a screen exists.
void updateStatusScreen() {
  // No buffer to draw into. The rover drives perfectly well without a screen.
  if (!displayReady) {
    return;
  }

  // The upload owns the panel for as long as it has something to report, and
  // its result stays up afterwards rather than being replaced the moment the
  // network comes back - the operator has to be able to read it.
  const bool uploadOwnsScreen = uploadPhase != UploadPhase::Idle;

  if (uploadOwnsScreen) {
    const uint8_t detail = (uploadPhase == UploadPhase::Waiting)
                               ? uploadSecondsLeft()
                               : uploadSent;

    if (uploadPhase == lastDrawnPhase && detail == lastDrawnDetail) {
      return;
    }

    lastDrawnPhase = uploadPhase;
    lastDrawnDetail = detail;

    // Forces the status screen to redraw once the upload releases the panel.
    lastDrawnStations = DISPLAY_STATIONS_UNKNOWN;

    showUploadScreen();
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
void displayBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
}
