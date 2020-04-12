#include <Arduino.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

// digital pins
#define OIL_PRESSURE_CLK 26
#define OIL_PRESSURE_DIO 27
#define OIL_PRESSURE_WINDOW 5
#define OIL_VIN_PIN 0

#define TEST_DELAY   100

// https://www.makerguides.com/wp-content/uploads/2019/08/7-segment-display-annotated.jpg

// anes
// thet
// ic
const uint8_t SEG_SHIT[] = {
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_F | SEG_E | SEG_B | SEG_C | SEG_G,
  SEG_B | SEG_C,
  SEG_F | SEG_E | SEG_A
  };
const uint8_t SEG_FAIL[] = {
  SEG_A | SEG_F | SEG_E | SEG_G,
  SEG_A | SEG_F | SEG_B | SEG_E | SEG_C | SEG_G,
  SEG_B | SEG_C,
  SEG_F | SEG_E | SEG_D
  };



TM1637Display oilPressure(OIL_PRESSURE_CLK, OIL_PRESSURE_DIO);

void setup() {
  oilPressure.setBrightness(0x0f);
  oilPressure.setSegments(SEG_FAIL);

  delay(5000);

  uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
  oilPressure.setSegments(blank);
}

void loop() {
  oilPressure.showNumberDec(readOilPSI());

  delay(TEST_DELAY);
}

CircularBuffer<float,OIL_PRESSURE_WINDOW> oilPSIWindow;

float readOilPSI() {
  float psi = ((float) analogRead(OIL_VIN_PIN) / 1024) * 200;
  oilPSIWindow.push(psi);

  float total = 0;
  for (int i = 0; i <= oilPSIWindow.size(); i++) {
    total += oilPSIWindow[i];
  }

  return max(total / oilPSIWindow.size(), 0);
}
