#include <Arduino.h>
#include <math.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

/* Radio */
#include <SPI.h>
#include <RH_RF95.h>

#define WINDOW_SIZE 10

// digital pins
#define IDIOT_LIGHT 29
#define OIL_PRESSURE_CLK 25
#define OIL_PRESSURE_DIO 23
#define OIL_PRESSURE_VIN_PIN 0

#define OIL_TEMP_CLK 26
#define OIL_TEMP_DIO 23
#define OIL_TEMP_PIN 1
#define OIL_TEMP_R2_K_OHM 10
#define OIL_TEMP_NOMINAL_RESISTANCE 50000
#define OIL_TEMP_NOMINAL_TEMP 25
#define OIL_TEMP_BETA_COEFFICIENT 3892

#define AUX_MESSAGE_CLK 22
#define AUX_MESSAGE_DIO 23

#define COOLANT_PRESSURE_CLK 24
#define COOLANT_PRESSURE_DIO 23
#define COOLANT_PRESSURE_VIN_PIN 2

#define TEST_DELAY 10

RH_RF95 rf95;
bool radioAvailable;

/*
|Code|English             |Unit                 |
|T_C |Coolant temperature |Tenths of a degree F |
|P_C |Coolant pressure    |PSI                  |
|T_O |Oil temperature     |Tenths of a degree F |
|P_O |Oil pressure        |PSI                  |
|RPM |RPM                 |RPM                  |
|SPD |GPS Speed           |Tenths of a MPH      |
|TIM |GPS Lap Time        |Deciseconds          |
|FLT |Fault               |Fault code, see below|

Fault code
Digit 0 is the least significant, 5 is the most significant.
|Digit|Meaning
|0    |Idiot light. 0: Light off 1: Light on    |
|1    |Oil pres warning. 0: Nominal 1: Fault    |
|2    |Oil temp warning. 0: Nominal 1: Fault    |
|3    |Coolant temp warning. 0: Nominal 1: Fault|
|4    |Coolant pres warning. 0: Nominal 1: Fault|
*/

char* RADIO_MSG_COOLANT_PRES = "P_C";
char* RADIO_MSG_OIL_TEMP = "T_O";
char* RADIO_MSG_OIL_PRES = "P_O";
char* RADIO_MSG_FAULT = "FLT";

char radioMsgBuf[32];

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
TM1637Display auxMessageDisplay(AUX_MESSAGE_CLK, AUX_MESSAGE_DIO);

void setup() {
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available

  pinMode(IDIOT_LIGHT, OUTPUT);
  digitalWrite(IDIOT_LIGHT, HIGH);

  oilPressureDisplay.setBrightness(0x0f);
  oilTemperatureDisplay.setBrightness(0x0f);
  coolantPressureDisplay.setBrightness(0x0f);
  auxMessageDisplay.setBrightness(0x0f);

  oilPressureDisplay.setSegments(SEG_OIL);
  coolantPressureDisplay.setSegments(SEG_COOL);
  oilTemperatureDisplay.setSegments(SEG_OIL);
  auxMessageDisplay.showNumberDec(8888);

  delay(2000);
  
  if (rf95.init()) {
    radioAvailable = true;
    rf95.setTxPower(20, false);
    auxMessageDisplay.showNumberDec(1337);
  } else {
    radioAvailable = false;
    Serial.println("radio init failed");
  }

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
  sendRadioMessage(RADIO_MSG_OIL_PRES, (uint16_t)oilPressure);

  coolantPressureDisplay.showNumberDec(coolantPressure);
  sendRadioMessage(RADIO_MSG_COOLANT_PRES, (uint16_t)coolantPressure);

  oilTemperatureDisplay.showNumberDec(oilTemperature);
  sendRadioMessage(RADIO_MSG_OIL_TEMP, (uint16_t)oilTemperature);

  showIdiotLight(oilPressure, coolantPressure, oilTemperature);

  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN + 1];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len)) {
      buf[len] = 0;
      Serial.println((char*)buf);
      Serial.print("RSSI: ");
      Serial.println(rf95.lastRssi(), DEC);
    } else {
      Serial.println("recv failed");
    }
  }
  
  delay(TEST_DELAY);
}

void sendRadioMessage(char* msg, uint16_t data) {
  if (!radioAvailable) {
    return;
  }

  int bytesWritten = sprintf(radioMsgBuf, "%s:%05u\n", msg, data);

  Serial.print("Radio message:");
  Serial.print(radioMsgBuf);

  rf95.send(radioMsgBuf, bytesWritten);
  rf95.waitPacketSent();
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
  bool oilPressBad = oilPressure < 15;
  bool coolantPressBad = coolantPressure < 5;
  bool oilTempBad = oilTemperature > 240;
  bool idiotLight =
    oilPressBad ||
    coolantPressBad ||
    oilTempBad;

  if (idiotLight) {
    digitalWrite(IDIOT_LIGHT, HIGH);
  } else {
    digitalWrite(IDIOT_LIGHT, LOW);
  }

  sendRadioMessage(
    RADIO_MSG_FAULT,
    idiotLight &
    oilPressBad << 1 &
    oilTempBad << 2 &
    /* coolantTempBad << 3 & */
    coolantPressBad << 4
  );
}

double avg(CircularBuffer<double,WINDOW_SIZE> &cb) {
  if (cb.size() == 0) return 0;
  double total = 0;
  for (int i = 0; i <= cb.size(); i++) {
    total += cb[i];
  }
  return total / cb.size();
}
