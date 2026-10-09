#include <Arduino.h>
#include "adc_lut.h"  // replace with the table YOUR board printed; this one is from the author's board

// Prints raw and corrected readings in Serial Plotter format (label:value).
// Feed it anything 0..3.3 V on GPIO35, e.g. a potentiometer wiper.

#define ADC_PIN 35

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // must match the settings the table was generated with
}

void loop() {
  int raw = analogRead(ADC_PIN);
  Serial.printf("raw:%d corrected:%.1f\n", raw, ADC_LUT[raw]);
  delay(100);
}
