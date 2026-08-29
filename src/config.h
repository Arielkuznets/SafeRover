#pragma once

// Every tunable number in the rover. constexpr throughout, which is what lets
// the static_asserts refuse an impossible combination before it is flashed. The
// measurements behind these values are in docs/journal.md.

#include <Arduino.h>

// --- Ultrasonic --- A 10 us HIGH on TRIG starts a measurement; the short LOW
// in front gives the module one clean rising edge.
constexpr unsigned long TRIG_SETTLE_US = 2;
constexpr unsigned long TRIG_PULSE_US = 10;

// pulseIn defaults to a one-second timeout, which would freeze the loop for a
// second on every missed echo. The module reaches 400 cm, which is 23.3 ms.
constexpr unsigned long ECHO_TIMEOUT_US = 25000;

constexpr float SOUND_SPEED_CM_PER_US = 0.0343f; // 343 m/s at room temperature

// A median rather than an average: ultrasonic errors land far from the truth,
// and one wild value drags an average but cannot reach the middle of a median.
constexpr uint8_t DISTANCE_SAMPLES = 3;
constexpr uint8_t DISTANCE_MIN_VALID_SAMPLES = 2;

// Bursts need spacing, or the next measurement hears the tail of the previous
// one and reads it as a much closer object.
constexpr unsigned long SAMPLE_SPACING_MS = 5;

// --- OLED ---
constexpr uint8_t OLED_ADDRESS = 0x3C; // confirmed by tools/i2c_scanner

// Getting the size wrong does not fail loudly - the library still draws, into a
// buffer the wrong shape.
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 32;

// 100 kHz, not the library's 400. Long jumpers with no external pull-ups add
// capacitance that rounds off the edges; at 400 kHz there is no time to settle
// and the controller receives corrupted commands.
constexpr uint32_t I2C_CLOCK_HZ = 100000UL;

constexpr int16_t OLED_TEXT_LEFT = 4;
constexpr int16_t OLED_TEXT_TOP = 4;
constexpr int16_t OLED_LINE_SPACING = 9;

// Three lines of 20 characters is what fits inside the border.
constexpr char SCREEN_TITLE[] = "SafeRover";
constexpr char SCREEN_PHONE_YES[] = "phone: connected";
constexpr char SCREEN_PHONE_NO[] = "phone: waiting";
constexpr char SCREEN_AP_FAILED[] = "WIFI AP FAILED";

constexpr int8_t OLED_RESET_PIN = -1; // this module exposes no reset pin

// --- ADC ---
constexpr uint8_t ADC_RESOLUTION_BITS = 12; // 0-4095, with ADC_11db for 3.3 V

// Derived, so the two cannot drift apart. A running minimum starts here.
constexpr uint16_t ADC_MAX_VALUE = (1U << ADC_RESOLUTION_BITS) - 1;

// The ADC is noisy - repeated reads of a steady voltage differ by tens of
// counts.
constexpr uint8_t LINE_SENSOR_SAMPLES = 16;

// --- PWM (LEDC) --- The eight channels share four timers in pairs, and the
// frequency belongs to the timer: two channels on one timer cannot run at
// different frequencies, and setting one silently changes the other. These
// three sit on separate timers.
constexpr uint8_t PWM_CHANNEL_BRAKE = 0;       // timer 0
constexpr uint8_t PWM_CHANNEL_MOTOR_LEFT = 2;  // timer 1, ENA
constexpr uint8_t PWM_CHANNEL_MOTOR_RIGHT = 4; // timer 2, ENB

constexpr uint32_t PWM_FREQUENCY_HZ = 5000; // above what the eye follows
constexpr uint8_t PWM_RESOLUTION_BITS = 8;  // 256 steps
constexpr uint32_t PWM_MAX_DUTY = (1UL << PWM_RESOLUTION_BITS) - 1;

// Percentages are the readable unit; the hardware wants raw counts. Here rather
// than in motors.h on purpose: it is arithmetic on a constant with no hardware
// in it, so the lane keeping and the web handlers can convert without being
// handed a declaration of drive().
constexpr uint32_t dutyFromPercent(uint8_t percent) {
  return (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
}

// --- Timing --- Deliberately below what the loop can deliver. The ceiling is
// readEchoMedian, which blocks up to 85 ms waiting out three ultrasonic
// timeouts - and does so precisely when the road ahead is clear. Asking for
// less than the ceiling is what makes the average report the rate ACHIEVED.
constexpr unsigned long LINE_READ_INTERVAL_MS = 20;

// The window the achieved read rate is averaged over, not a reporting rate.
constexpr unsigned long LINE_SUMMARY_INTERVAL_MS = 500;

// The ask is cheap and the redraw is not, so this costs almost nothing.
constexpr unsigned long DISPLAY_CHECK_INTERVAL_MS = 500;

// About 4.2 cm of ground between reads at full power. One reading blocks up to
// 85 ms, longer than this, so during an echo outage the measurement sets the
// pace and this number does not.
constexpr unsigned long DISTANCE_READ_INTERVAL_MS = 50;

constexpr unsigned long BUZZER_TEST_MS = 300;

// Above the 1-20 ms a tactile switch bounces for, below the 100-150 ms gap
// between even fast human presses.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;

// --- Wi-Fi access point --- The rover makes its own network rather than
// joining one: institutional networks need browser logins, hide passwords, or
// isolate clients from each other, and any of those takes the phone control
// down at the worst moment.
constexpr char WIFI_AP_SSID[] = "Saferover";

// The passphrase is in src/secrets.h, which is not committed - this repository
// is public. WPA2 refuses anything under eight characters, and the access point
// then fails to come up rather than falling back to open.

// One device at a time. Two operators commanding at once is genuinely unsafe:
// one brakes while the other accelerates and the rover obeys whichever packet
// landed last. Capping it in the radio means the command code never has to
// arbitrate, because the second phone cannot associate at all.
constexpr uint8_t WIFI_AP_MAX_CLIENTS = 1;

// softAP() takes these positionally ahead of the client limit, so both have to
// be given to reach it. Hiding an SSID is not security, only inconvenience.
constexpr uint8_t WIFI_AP_CHANNEL = 1;
constexpr bool WIFI_AP_HIDDEN = false;

// --- Web server ---
constexpr uint16_t WEB_SERVER_PORT = 80; // what a browser assumes

// Quick enough to feel live, slow enough that answering stays cheap: every
// request is time taken from the loop, and that loop runs the AEB.
constexpr unsigned long WEB_POLL_INTERVAL_MS = 500;

// Coupled to the poll rate and has to stay far below it. handleClient() takes
// at most ONE pending request per call, so if the two were close, any
// hesitation would leave a request waiting, a second would arrive on top, and
// the backlog would grow instead of draining.
constexpr unsigned long WEB_HANDLE_INTERVAL_MS = 20;

// --- Driving commands --- How long the rover accepts silence before stopping
// itself. Nothing in this system decays on its own, so a rover whose operator
// vanished would drive until the battery ran out. The page sends several times
// faster, so three consecutive messages have to be lost before it stops by
// mistake. About 42 cm of ground at full power.
constexpr unsigned long COMMAND_TIMEOUT_MS = 500;

// Not what makes the controls feel responsive - presses and releases send
// immediately. Its only job is to keep proving the channel is alive, so its
// ratio to the timeout above is the only thing about it that matters.
constexpr unsigned long COMMAND_SEND_INTERVAL_MS = 120;

// The slowest setting worth offering. Below this the torque never overcomes
// friction and the gearbox, so the motors sit drawing current and whining.
// Physical rather than a preference, measured on the floor on the rover's own
// supply. The slider will not go below it and the firmware refuses anything
// below it that arrives anyway.
constexpr uint8_t DRIVE_MIN_PERCENT = 62;

// Kept clear of the floor rather than on it: 62 is where the wheels were seen
// to start, which is an edge rather than a safe setting. 75 is about 50 cm/s.
constexpr uint8_t DRIVE_START_PERCENT = 75;

static_assert(DRIVE_START_PERCENT >= DRIVE_MIN_PERCENT,
              "slider default must not sit below the floor");

// --- AEB (autonomous emergency braking) --- Three stages by distance,
// calibrated by experiment on the floor.
//
// The margin covers the age of the reading acted on: 50 ms between reads plus
// up to 85 ms taking one, about 11 cm at full power. Stop also stays clear of
// the blind zone under ~2 cm.
//
// What no threshold can cover is an obstacle never seen. Ultrasound works by
// reflection, so a surface angled past roughly 15 degrees sends the burst
// sideways and a soft one absorbs it. The dangerous half is handled - a lost
// echo after a close reading goes to Stop - and the rest is the price of not
// stopping in every open room.
constexpr float AEB_WARN_CM = 90.0f;
constexpr float AEB_SLOW_CM = 55.0f;
constexpr float AEB_STOP_CM = 35.0f;

// Without it a reading resting on a threshold flickers across it on noise
// alone. Entering is immediate, leaving is reluctant: escalating late is a
// collision.
constexpr float AEB_HYSTERESIS_CM = 8.0f;

constexpr float AEB_WARN_EXIT_CM = AEB_WARN_CM + AEB_HYSTERESIS_CM;
constexpr float AEB_SLOW_EXIT_CM = AEB_SLOW_CM + AEB_HYSTERESIS_CM;
constexpr float AEB_STOP_EXIT_CM = AEB_STOP_CM + AEB_HYSTERESIS_CM;

static_assert(AEB_STOP_CM < AEB_SLOW_CM && AEB_SLOW_CM < AEB_WARN_CM,
              "AEB stages must escalate as the distance shrinks");

// An exit above the next entry would re-enter the stage above on the way out,
// leaving the rover oscillating between two stages instead of settling.
static_assert(AEB_STOP_EXIT_CM < AEB_SLOW_CM && AEB_SLOW_EXIT_CM < AEB_WARN_CM,
              "hysteresis must not reach into the next stage");

// One missed echo is ordinary, and readEchoMedian already needs two of three.
// What three come to in time does NOT follow from DISTANCE_READ_INTERVAL_MS: a
// failed reading runs all three pulseIn calls to their full timeout, about 85
// ms, so three is roughly 255 ms - about 21 cm at full power.
constexpr uint8_t AEB_MISSED_READS_LIMIT = 3;

// At 90 cm the echo has travelled 180 cm and is weak, so a glance off a wall to
// the side comes back at the same order of magnitude and the buzzer chirps on a
// reading that meant nothing. An early warning can afford to confirm itself;
// braking cannot, so Slow and Stop stay immediate.
constexpr uint8_t AEB_WARN_CONFIRM_READS = 2;

// The Slow ceiling has to stay above the floor: under it the rover does not
// slow down, it stops, and a stage that quietly becomes the stage below is
// worse than no stage.
constexpr uint8_t AEB_SLOW_PERCENT = 66;
static_assert(AEB_SLOW_PERCENT >= DRIVE_MIN_PERCENT,
              "the slow stage must still be able to move the rover");

// Absolute, and deliberately not derived from the read interval. The question
// is how long may have passed since the reading, which follows from how long a
// measurement takes - up to 85 ms - not from how often one is started.
constexpr unsigned long AEB_READING_MAX_AGE_MS = 300;

// --- LKA calibration --- Ignored before any reading is kept: a finger is
// leaving the rover and the chassis is still rocking on its springs.
constexpr unsigned long CAL_SETTLE_MS = 200;

// Short on purpose: readLineAveraged already averages 16 samples, and drift
// between measurements throws away any precision that standing still longer
// would buy.
constexpr unsigned long CAL_SAMPLE_MS = 600;

constexpr unsigned long ALERT_BEEP_MS = 600; // the non-AEB buzzer

// Two sensors on the same white differ by a few hundred counts; one sitting on
// a stripe puts the difference near 1800. This sits between them. It compares a
// difference and not a level, which is the whole reason it can be a fixed
// number at all - levels move by more than a thousand counts between rooms.
constexpr int16_t CAL_MAX_RESTING_DIFF = 800;

// Standing still wanders by tens of counts; being held or nudged produces
// hundreds. This is what stops a zero point being averaged out of readings
// taken while the rover was moving - a number that would look reasonable and be
// wrong.
constexpr uint16_t CAL_MAX_SPREAD = 250;

// --- LKA --- The error the correction is scaled against. Lowering it RAISES
// the gain: correction = Kp * error / this, so it is not a unit conversion and
// cannot be re-set without re-tuning Kp.
//
// UNVALIDATED, and knowingly so. A sensor fully on a stripe measures about
// 1800, which is a quantity that was measured; 600 was taken from a run whose
// error range is now known to have been held small by the watchdog fault. The
// rover runs well on it, so it stands - but if the error is ever seen to reach
// much higher, this returns to 1800 with Kp scaled to match. Session 24.
constexpr int32_t LINE_DIFF_FULL_SCALE = 600;

// Below this, nothing is corrected. Inside the lane neither sensor has reached
// a stripe, but a difference always has some value, so without this the rover
// would steer against its own noise.
//
// It assumes the 25 cm lane, which is a specification and not a description of
// one track: rebuild it narrower and this looks like a firmware fault with not
// one line changed. Session 24, fault 3.
//
// It is also what keeps this lane keeping rather than lane centering: it
// intervenes on approach to a marking, it does not hold the middle.
constexpr int32_t LKA_DEADBAND = 200;

// Found on the floor, not chosen. Kp is the duty difference between the two
// sides at the far end of the error range, so it moves with
// LINE_DIFF_FULL_SCALE and the two cannot be read apart.
//
// A base on the stall floor is a trap: it leaves no room underneath, so every
// correction puts the slower side below it and the rover judders instead of
// steering.
constexpr uint8_t LKA_KP_DEFAULT = 45;
constexpr uint8_t LKA_BASE_PERCENT_DEFAULT = 85;
static_assert(LKA_BASE_PERCENT_DEFAULT > DRIVE_MIN_PERCENT,
              "a base speed on the stall floor leaves no room to correct");

// A guard against a typo in a text field on a phone, not a tuning limit.
constexpr uint8_t LKA_KP_MAX = 120;
static_assert(LKA_KP_MAX <= PWM_MAX_DUTY,
              "a Kp above full duty cannot reach the motors");

// The one place the differential approach falls back on absolute levels,
// because a difference cannot tell both sensors on white from both on black -
// near zero either way, and the second means the rover has left the track.
// There is no illumination-proof way to make that distinction, hence the
// generous margin and a calibration that re-measures the white it compares
// against.
constexpr uint16_t LKA_OFF_LANE_MARGIN = 900;
