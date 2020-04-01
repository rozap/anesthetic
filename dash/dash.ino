#include <Arduino.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

// digital pins
#define OIL_PRESSURE_CLK 26
#define OIL_PRESSURE_DIO 27
#define OIL_PRESSURE_WINDOW 5

#define TEST_DELAY   10

const uint8_t SEG_HI[] = {
  SEG_F | SEG_E | SEG_G | SEG_C, // h
  SEG_C,   // i
  0x00, // blank
  0x00 // blank
  };

TM1637Display oilPressure(OIL_PRESSURE_CLK, OIL_PRESSURE_DIO);

void setup() {
  oilPressure.setBrightness(0x0f);
  oilPressure.setSegments(SEG_HI);

  delay(5000);

  uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
  oilPressure.setSegments(blank);
}

void loop() {
  oilPressure.showNumberDec(readOilPSI());

  delay(TEST_DELAY);
}

// from https://www.autometer.com/sensor_specs
// PSI   Ohms
// 0     250
// 25    158
// 50    111
// 75    75
// 100   43
CircularBuffer<float,OIL_PRESSURE_WINDOW> oilPSIWindow;

int ohms = 43;
int readOilSensorResistance() {
  // replace this with the actual reading from the analog pin
  ohms += 1;
  if (ohms > 250) {
    ohms = 43;
  }
  return ohms;
}

float readOilPSI() {
  float psi = -0.4760 * readOilSensorResistance() + 110.6;
  oilPSIWindow.push(psi);

  float total = 0;
  for (int i = 0; i <= oilPSIWindow.size(); i++) {
    total += oilPSIWindow[i];
  }

  return max(total / oilPSIWindow.size(), 0);
}
