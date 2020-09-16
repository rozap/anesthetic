#include <Arduino.h>
#include <math.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

#define WINDOW_SIZE 30

// digital pins
#define IDIOT_LIGHT 30
#define OIL_PRESSURE_CLK 22
#define OIL_PRESSURE_DIO 23
#define OIL_PRESSURE_VIN_PIN 0

#define OIL_TEMP_CLK 24
#define OIL_TEMP_DIO 25
#define OIL_TEMP_PIN 1
#define OIL_TEMP_R1_K_OHM 10
#define OIL_TEMP_NOMINAL_RESISTANCE 50000
#define OIL_TEMP_NOMINAL_TEMP 25
#define OIL_TEMP_BETA_COEFFICIENT 3892

#define COOLANT_PRESSURE_CLK 26
#define COOLANT_PRESSURE_DIO 27
#define COOLANT_PRESSURE_VIN_PIN 2

#define TEST_DELAY 5

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

const uint8_t SEG_COOL[] = {
  SEG_A | SEG_F | SEG_E | SEG_D,
  SEG_A | SEG_F | SEG_B | SEG_E | SEG_C | SEG_D,
  SEG_A | SEG_F | SEG_B | SEG_E | SEG_C | SEG_D,
  SEG_F | SEG_E | SEG_D
};

const uint8_t SEG_OIL[] = {
  SEG_A | SEG_F | SEG_B | SEG_E | SEG_C | SEG_D,
  SEG_F | SEG_E,
  SEG_F | SEG_E | SEG_D,
  0x00
};

const uint8_t SEG_PSI[] = {
  SEG_A | SEG_F | SEG_B | SEG_G | SEG_E,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_F | SEG_E,
  0x00
};

const uint8_t SEG_TP[] = {
  SEG_A | SEG_B | SEG_C,
  SEG_A | SEG_F | SEG_B | SEG_G | SEG_E,
  0x00,
  0x00
};



TM1637Display oilPressureDisplay(OIL_PRESSURE_CLK, OIL_PRESSURE_DIO);
TM1637Display coolantPressureDisplay(COOLANT_PRESSURE_CLK, COOLANT_PRESSURE_DIO);
TM1637Display oilTemperatureDisplay(OIL_TEMP_CLK, OIL_TEMP_DIO);

void setup() {
  pinMode(IDIOT_LIGHT, OUTPUT);
  digitalWrite(IDIOT_LIGHT, HIGH);

  oilPressureDisplay.setBrightness(0x0f);
  oilTemperatureDisplay.setBrightness(0x0f);
  coolantPressureDisplay.setBrightness(0x0f);

  oilPressureDisplay.setSegments(SEG_OIL);
  coolantPressureDisplay.setSegments(SEG_COOL);
  oilTemperatureDisplay.setSegments(SEG_OIL);

  delay(2000);

  oilPressureDisplay.setSegments(SEG_PSI);
  coolantPressureDisplay.setSegments(SEG_PSI);
  oilTemperatureDisplay.setSegments(SEG_TP);

  delay(2000);

  uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
  oilPressureDisplay.setSegments(blank);
  coolantPressureDisplay.setSegments(blank);
  oilTemperatureDisplay.setSegments(blank);
}

void loop() {
  double oilPressure = readOilPSI();
  double coolantPressure = readCoolantPSI();
  double oilTemperature = readOilTemp();

  oilPressureDisplay.showNumberDec(oilPressure);
  coolantPressureDisplay.showNumberDec(coolantPressure);
  oilTemperatureDisplay.showNumberDec(oilTemperature);

  showIdiotLight(oilPressure, coolantPressure, oilTemperature);
  delay(TEST_DELAY);
}

CircularBuffer<double,WINDOW_SIZE> oilPSIWindow;
double readOilPSI() {
  double psi = (((double)(analogRead(OIL_PRESSURE_VIN_PIN) - 122)) / 1024) * 200;
  oilPSIWindow.push(psi);
  return avg(oilPSIWindow);
}


CircularBuffer<double,WINDOW_SIZE> coolantPSIWindow;
double readCoolantPSI() {
  double psi = (((double)(analogRead(COOLANT_PRESSURE_VIN_PIN) - 122)) / 1024) * 80;
  coolantPSIWindow.push(psi);
  return avg(coolantPSIWindow);
}


// https://www.amazon.com/Universal-Water-Temperature-Sensor-Sender/dp/B0771KB6FN/ref=sr_1_17?dchild=1&keywords=oil+temperature+sensor&qid=1590294751&sr=8-17
CircularBuffer<double,WINDOW_SIZE> oilTempWindow;
double readOilTemp() {
  int vin = 5;
  double vout = (double)((analogRead(OIL_TEMP_PIN) * vin) / 1024);
  double ohms = (OIL_TEMP_R1_K_OHM * 1000) * (1 / ((vin - vout) - 1));
  double tempC = 1 / ( ( log( ohms / OIL_TEMP_NOMINAL_RESISTANCE )) / OIL_TEMP_BETA_COEFFICIENT + 1 / ( OIL_TEMP_NOMINAL_TEMP + 273.15 ) ) - 273.15;
  double tempF = (tempC * 1.8) + 32;
  oilTempWindow.push(tempF);
  return avg(oilTempWindow);
}


double showIdiotLight(double oilPressure, double coolantPressure, double oilTemperature) {
  if (
    (oilPressure < 15) ||
    (coolantPressure < 5) ||
    (oilTemperature > 240)
  ) {
    digitalWrite(IDIOT_LIGHT, HIGH);
  } else {
    digitalWrite(IDIOT_LIGHT, LOW);
  }
}

double avg(CircularBuffer<double,WINDOW_SIZE> &cb) {
  if (cb.size() == 0) return 0;
  double total = 0;
  for (int i = 0; i <= cb.size(); i++) {
    total += cb[i];
  }
  return total / cb.size();
}
