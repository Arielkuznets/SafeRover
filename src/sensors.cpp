#include "sensors.h"

#include "config.h"
#include "pins.h"

unsigned long lastLineRead = 0;
unsigned long lastLineSummary = 0;
unsigned long lastDistanceRead = 0;

// The last readings the loop took, held at file scope so a request handler can
// report them without measuring anything itself. Reading a sensor inside a
// handler would let a browser decide when pulseIn fires, and two ultrasonic
// bursts close together each measure the other's echo. Measuring stays the
// loop's job; the page only reports what the loop already found.
//
// echoDurationValue keeps the meaning zero carries everywhere in this project:
// no echo came back, which is not a distance.
uint16_t lineLeftValue = 0;
uint16_t lineRightValue = 0;
unsigned long echoDurationValue = 0;

// Per sensor rather than one shared pair. A shared span would let a sensor that
// reached a stripe cover for one that never did - exactly the silent failure
// the calibration has to be able to refuse.
//
// The minimums start at the top of the ADC range and the maximums at the
// bottom, so the first reading replaces the seed rather than competing with it.
uint16_t lineLeftMin = ADC_MAX_VALUE;
uint16_t lineLeftMax = 0;
uint16_t lineRightMin = ADC_MAX_VALUE;
uint16_t lineRightMax = 0;

// The worst interval observed between two consecutive line reads, and the
// average over the last window. The loop asks for a rate; what it gets is that
// rate plus whatever blocked in between, and readEchoMedian waiting out three
// ultrasonic timeouts is by far the largest of those. The maximum catches the
// worst case, the average says whether that case was an outlier.
unsigned long lineGapMaxMs = 0;
uint16_t lineReadsInWindow = 0;
unsigned long lineAvgGapMs = 0;

// One reader for both sensors - the same part on the same ADC, so the pin is
// the only thing that differs.
uint16_t readLineAveraged(uint8_t pin) {
  uint32_t total = 0;

  for (uint8_t i = 0; i < LINE_SENSOR_SAMPLES; i++) {
    total += analogRead(pin);
  }

  return static_cast<uint16_t>(total / LINE_SENSOR_SAMPLES);
}

// One measurement: the width of the ECHO pulse in microseconds, or 0 when
// nothing came back before the timeout.
//
// delayMicroseconds is not a scheduling delay here - the 10 us pulse *is* the
// signal the module waits for, and holding the line is the only way to produce
// it.
static unsigned long readEchoDuration() {
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(TRIG_SETTLE_US);

  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(TRIG_PULSE_US);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  return pulseIn(PIN_ULTRASONIC_ECHO, HIGH, ECHO_TIMEOUT_US);
}

// The median of the samples that came back, or 0 when too few did.
//
// The delay between samples is part of the measuring protocol, in the same
// sense as the microsecond delays inside the trigger pulse: a burst fired
// before the previous one has died away measures the old echo, not the new one.
unsigned long readEchoMedian() {
  unsigned long valid[DISTANCE_SAMPLES];
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < DISTANCE_SAMPLES; i++) {
    if (i > 0) {
      delay(SAMPLE_SPACING_MS);
    }

    const unsigned long sample = readEchoDuration();

    // A timeout carries no distance, so it is dropped rather than sorted with
    // the rest - a 0 would sink to the bottom and drag the median with it.
    if (sample != 0) {
      valid[validCount++] = sample;
    }
  }

  // With most of the samples gone, reporting a distance from the one that
  // survived would be a guess presented as a measurement. A safety system is
  // better off declaring it has no information: the caller already knows how to
  // handle "no reading", and 0 is exactly that signal.
  if (validCount < DISTANCE_MIN_VALID_SAMPLES) {
    return 0;
  }

  // Never more than three elements, so a plain insertion sort is the clearest
  // thing that works.
  for (uint8_t i = 1; i < validCount; i++) {
    const unsigned long key = valid[i];
    uint8_t j = i;

    while (j > 0 && valid[j - 1] > key) {
      valid[j] = valid[j - 1];
      j--;
    }

    valid[j] = key;
  }

  // An even count has no real middle, and the tie-break is the lower of the
  // two. Averaging them would let a bad sample pull the result, which is the
  // whole thing the median was chosen to avoid - and of two candidate distances
  // the nearer one is the safe one for a brake to act on.
  const uint8_t medianIndex =
      (validCount % 2 == 1) ? (validCount / 2) : (validCount / 2 - 1);

  return valid[medianIndex];
}

// The pulse times a round trip, so the distance is half what the sound covered.
float distanceFromDuration(unsigned long durationUs) {
  return (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;
}

// Parks TRIG low so the module is not looking at a half-raised line before the
// first measurement, and sets the ADC up for the line sensors.
//
// analogSetAttenuation, not the per-pin form. The per-pin call reconfigures a
// channel that already exists, and the core only opens a channel on the first
// analogRead of that pin - so calling it here found nothing to reconfigure and
// was dropped with an error on every boot. This form sets the value the
// channels are built with.
void sensorsBegin() {
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetAttenuation(ADC_11db);
}
