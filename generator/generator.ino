#include <Arduino.h>

// ESP32 ADC linearity LUT generator, version 3.
// https://github.com/zeyadnabhan/esp32-adc-lut
// Rewrite of e-tinkers/esp32-adc-calibrate (Henry Cheung), itself based on Helmut Weber's
// MacLeod-D/ESP32-ADC. Same idea, new implementation; see README for what changed and why.
//
// Wiring: jumper GPIO25 (DAC1) -> GPIO35 (ADC1_CH7). Nothing else connected to either pin.
// Output: `const float ADC_LUT[4096]` on Serial (115200), same format as v2, so the
// upstream example works unchanged:  float corrected = ADC_LUT[analogRead(pin)];
//
// Changes vs v2:
// - Exact piecewise-linear inversion of the measured curve instead of an 84M-step
//   nearest-value search. Also removes v2's Res2 bug (steps of 1/10 read back as 1/5).
// - Plain averaging over interleaved sweeps instead of an EMA that starts from 0.
// - Measured curve forced monotonic before inversion (noise can't fold the table).
// - Verification sweep printing the error before/after correction.
// - dacWrite() instead of driver/dac.h, so it builds on Arduino-ESP32 core 2.x and 3.x.
//
// Caveat: the DAC is the reference, so the table corrects the DAC+ADC pair, not the
// ADC alone. For absolute accuracy, check a few points against a good multimeter.

#define DAC_PIN   25
#define ADC_PIN   35
#define SWEEPS    200   // full 0..255 sweeps, interleaved to average out drift
#define SAMPLES   8     // reads per DAC step per sweep
#define SETTLE_US 100   // DAC settle time before reading
// #define INT_LUT      // uncomment for `const int ADC_LUT[4096]` (rounded)

static float measured[257];  // mean raw ADC reading at DAC code k; [256] = endpoint

void measure() {
  static uint32_t sum[256];  // max 200*8*4095 = 6.5M, fits
  Serial.print(F("Measuring"));
  for (int s = 0; s < SWEEPS; s++) {
    for (int k = 0; k < 256; k++) {
      dacWrite(DAC_PIN, k);
      delayMicroseconds(SETTLE_US);
      for (int n = 0; n < SAMPLES; n++) sum[k] += analogRead(ADC_PIN);
    }
    if (s % 20 == 0) Serial.print('.');
  }
  Serial.println();

  for (int k = 0; k < 256; k++) {
    measured[k] = (float)sum[k] / (SWEEPS * SAMPLES);
    if (k > 0 && measured[k] < measured[k - 1]) measured[k] = measured[k - 1];
  }
  measured[256] = 4095.0f;  // same endpoint assumption as v2: DAC "256" reads full scale
}

// Raw ADC reading -> DAC scale (code k maps to 16*k, so 0..4095 like the raw value).
float invert(int raw) {
  int k = 0;
  while (k < 255 && measured[k + 1] < raw) k++;  // first segment whose top reaches raw
  float lo = measured[k], hi = measured[k + 1];
  if (hi <= lo) return 16.0f * k;                // flat (clipped) segment
  float t = constrain((raw - lo) / (hi - lo), 0.0f, 1.0f);
  return min(16.0f * (k + t), 4095.0f);
}

void verify() {
  float maxRaw = 0, maxCal = 0, sqRaw = 0, sqCal = 0;
  int n = 0;
  for (int k = 0; k < 256; k++) {
    dacWrite(DAC_PIN, k);
    delayMicroseconds(SETTLE_US);
    uint32_t s = 0;
    for (int i = 0; i < 64; i++) s += analogRead(ADC_PIN);
    int raw = (s + 32) / 64;
    if (raw == 0 || raw == 4095) continue;  // clipped ends: nothing to correct there
    float eRaw = raw - 16.0f * k, eCal = invert(raw) - 16.0f * k;
    maxRaw = max(maxRaw, fabsf(eRaw)); sqRaw += eRaw * eRaw;
    maxCal = max(maxCal, fabsf(eCal)); sqCal += eCal * eCal;
    n++;
  }
  if (n == 0) { Serial.println(F("Verify: no usable points, check the wiring.")); return; }
  Serial.printf("Verify over %d DAC steps (error vs DAC scale, in LSB):\n", n);
  Serial.printf("  raw       : max %.1f  rms %.1f\n", maxRaw, sqrtf(sqRaw / n));
  Serial.printf("  corrected : max %.1f  rms %.1f\n", maxCal, sqrtf(sqCal / n));
}

void printLut() {
  Serial.println();
#ifdef INT_LUT
  Serial.println(F("const int ADC_LUT[4096] = {"));
#else
  Serial.println(F("const float ADC_LUT[4096] = {"));
#endif
  for (int r = 0; r < 4096; r++) {
#ifdef INT_LUT
    Serial.print(lroundf(invert(r)));
#else
    Serial.print(invert(r), 2);
#endif
    if (r < 4095) Serial.print(',');
    if (r % 16 == 15) Serial.println();
  }
  Serial.println(F("};"));
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // full ~0..3.1 V range, the default, set explicitly
  delay(1000);

  measure();
  verify();
  printLut();
}

void loop() {}
