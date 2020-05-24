#include <Arduino.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

#define WINDOW_SIZE 30

// digital pins
#define OIL_PRESSURE_CLK 26
#define OIL_PRESSURE_DIO 27
#define OIL_PRESSURE_VIN_PIN 0

#define OIL_TEMP_CLK = 28
#define OIL_TEMP_DIO = 29
#define OIL_TEMP_PIN = 1
#define OIL_TEMP_R1_K_OHM = 10
#define OIL_TEMP_NOMINAL_RESISTANCE = 50000
#define OIL_TEMP_NOMINAL_TEMP = 25
#define OIL_TEMP_BETA_COEFFICIENT = 3892

#define TEST_DELAY   5

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



TM1637Display oilPressureDisplay(OIL_PRESSURE_CLK, OIL_PRESSURE_DIO);
TM1637Display oilTempDisplay(OIL_TEMP_CLK, OIL_TEMP_DIO);

void setup() {
  oilPressureDisplay.setBrightness(0x0f);
  oilPressureDisplay.setSegments(SEG_FAIL);

  oilTempDisplay.setBrightness(0x0f);
  oilTempDisplay.setSegments(SEG_SHIT);

  delay(5000);

  uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
  oilPressureDisplay.setSegments(blank);
  oilTempDisplay.setSegments(blank);
}

void loop() {
  oilPressureDisplay.showNumberDec(readOilPSI());
  oilTempDisplay.showNumberDec(readOilTemp());
  delay(TEST_DELAY);
}

CircularBuffer<double,WINDOW_SIZE> oilPSIWindow;
double readOilPSI() {
  double psi = (((double)(analogRead(OIL_PRESSURE_VIN_PIN) - 122)) / 1024) * 200;
  oilPSIWindow.push(psi);
  return avg(&oilPSIWindow);
}


CircularBuffer<double,WINDOW_SIZE> oilTempWindow;
double readOilTemp() {
  int vin = 5;
  double vout = (double)((analogRead(OIL_TEMP_PIN) * vin) / 1024);
  double ohms = (OIL_TEMP_R1_K_OHM * 1000) * (1 / ((vin - vout) - 1));
  double tempC = 1 / ( ( Math.log( ohms / OIL_TEMP_NOMINAL_RESISTANCE )) / OIL_TEMP_BETA_COEFFICIENT + 1 / ( OIL_TEMP_NOMINAL_TEMP + 273.15 ) ) - 273.15;
  double tempF = (tempC * 1.8) + 32;
  oilTempWindow.push(tempF);
  return avg(&oilTempWindow);
}

double avg(CircularBuffer<double,WINDOW_SIZE> &cb) {
  double total = 0;
  for (int i = 0; i <= cb->size(); i++) {
    total += cb[i];
  }
  return max(total / cb->size())
}