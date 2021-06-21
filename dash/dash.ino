#include <Arduino.h>
#include <math.h>
#include <TM1637Display.h>
#include <CircularBuffer.h>

/* Radio */
#include <SPI.h>
#include <RH_RF95.h>

/* Tach */
#include "Wire.h"

//#define SKIP_INTRO_ANIM

#define TACH_LIGHT_IDIOT (1<<15)
#define TACH_LIGHT_G1 (1<<14)
#define TACH_LIGHT_G2 (1<<13)
#define TACH_LIGHT_G3 (1<<12)
#define TACH_LIGHT_Y1 (1<<11)
#define TACH_LIGHT_Y2 (1<<10)
#define TACH_LIGHT_Y3 (1<<9)
#define TACH_LIGHT_R1 (1<<8)
#define TACH_LIGHT_R2 (1<<0)

// TODO Configure via radio?
#define REDLINE_RPM 6000
#define FIRST_LIGHT_RPM 1000

/* Sensor sampling */
#define SAMPLE_PERIOD_MS 250

// To avoid blocking on the radio, set this to a value
// that pretty much guarantees we'll have sent the packet
// by the time we try to send again.
#define RADIO_UPDATE_PERIOD_MS 2000
#define WINDOW_SIZE 10

/* Pin assignments */
// Must support interrupts. https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
#define TACH_SIGNAL_PIN 18
// It takes a fixed time for the tach ISR to record the time.
// Subtract that process time from the measured time.
// This is determined experimentally.
#define TACH_ISR_OFFSET_MICROS 2

#define VBAT_VIN_PIN 4
// Resistor divider ratio. Value read from ADC is multiplied by this
// to get real voltage.
#define VBAT_RATIO 3.03
#define VBAT_WARN_MIN 12.8
#define VBAT_WARN_MAX 15.0

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

RH_RF95 rf95;
bool radioAvailable;

/*
|Code|English             |Unit                 |
|T_C |Coolant temperature |Tenths of a degree F |
|P_C |Coolant pressure    |PSI                  |
|T_O |Oil temperature     |Tenths of a degree F |
|P_O |Oil pressure        |PSI                  |
|VBA |Battery voltage     |Millivolts           |
|RPM |RPM                 |RPM                  |
|SPD |GPS Speed           |Tenths of a MPH      |
|TIM |GPS Lap Time        |Deciseconds          |
|FLT |Fault               |Fault code, see below|

Fault code
Digit 0 is the least significant, 5 is the most significant.
|Digit|Meaning
|0    |Idiot light. 0: Light off 1: Light on    |
*/

const char* RADIO_MSG_COOLANT_PRES = "P_C";
const char* RADIO_MSG_OIL_TEMP = "T_O";
const char* RADIO_MSG_OIL_PRES = "P_O";
const char* RADIO_MSG_FAULT = "FLT";
const char* RADIO_MSG_BATTERY_VOLTAGE = "VBA";
const char* RADIO_MSG_RPM = "RPM";


char radioMsgBuf[32];

// https://www.makerguides.com/wp-content/uploads/2019/08/7-segment-display-annotated.jpg

// anes
// thet
// ic

const uint8_t SEG_ANES[] = {
  SEG_E | SEG_F | SEG_A | SEG_B | SEG_C | SEG_G,
  SEG_E | SEG_G | SEG_C,
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_D,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D
};

const uint8_t SEG_0THE[] = {
  0,
  SEG_A | SEG_F | SEG_E,
  SEG_F | SEG_E | SEG_G | SEG_C,
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_D
};

const uint8_t SEG_TIC0[] = {
  SEG_A | SEG_F | SEG_E,
  SEG_B | SEG_C,
  SEG_A | SEG_F | SEG_E | SEG_D,
  0
};

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

const uint8_t SEG_RSSI[] = {
  SEG_A | SEG_F | SEG_E,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_B | SEG_C
};

const uint8_t SEG_NO[] = {
  SEG_E | SEG_G | SEG_C,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
  0x00,
  0x00
};

const uint8_t SEG_INOP[] = {
  SEG_B | SEG_C,
  SEG_E | SEG_G | SEG_C,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
  SEG_E | SEG_F | SEG_A | SEG_B | SEG_G
};

const uint8_t SEG_RDIO[] = {
  SEG_A | SEG_F | SEG_E,
  SEG_G | SEG_E | SEG_D | SEG_C | SEG_B,
  SEG_C,
  SEG_E | SEG_G | SEG_C | SEG_D
};

const uint8_t SEG_BLANK[] = { 0, 0, 0, 0 };

TM1637Display oilPressureDisplay(OIL_PRESSURE_CLK, OIL_PRESSURE_DIO);
TM1637Display coolantPressureDisplay(COOLANT_PRESSURE_CLK, COOLANT_PRESSURE_DIO);
TM1637Display oilTemperatureDisplay(OIL_TEMP_CLK, OIL_TEMP_DIO);
TM1637Display auxMessageDisplay(AUX_MESSAGE_CLK, AUX_MESSAGE_DIO);

void setup() {
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available

  Wire.begin();
  tachInit();

  oilPressureDisplay.setBrightness(0, false);
  oilTemperatureDisplay.setBrightness(0, false);
  coolantPressureDisplay.setBrightness(0, false);
  auxMessageDisplay.setBrightness(0, false);
  oilPressureDisplay.setSegments(SEG_BLANK);
  coolantPressureDisplay.setSegments(SEG_BLANK);
  oilTemperatureDisplay.setSegments(SEG_BLANK);
  auxMessageDisplay.setSegments(SEG_BLANK);

  #ifndef SKIP_INTRO_ANIM
  tachBootAnimation();
  #endif
  
  if (rf95.init()) {
    Serial.println("radio init ok");
    radioAvailable = true;
    rf95.setTxPower(20, false);
    auxMessageDisplay.showNumberDec(1337);
  } else {
    radioAvailable = false;
    Serial.println("radio init failed");
  }

  #ifndef SKIP_INTRO_ANIM
  uint8_t d = 100;
  for (uint8_t brt = 1; brt<8; brt++) {
    oilTemperatureDisplay.setBrightness(brt);
    oilTemperatureDisplay.setSegments(SEG_ANES);
    delay(d);
  }
  for (uint8_t brt = 1; brt<8; brt++) {
    coolantPressureDisplay.setBrightness(brt);
    coolantPressureDisplay.setSegments(SEG_0THE);
    delay(d);
  }
  for (uint8_t brt = 1; brt<8; brt++) {
    auxMessageDisplay.setBrightness(brt);
    auxMessageDisplay.setSegments(SEG_TIC0);
    delay(d);
  }

  delay(250);

  for (uint8_t brt = 4; brt>0; brt--) {
    oilTemperatureDisplay.setBrightness(brt);
    oilTemperatureDisplay.setSegments(SEG_ANES);
    coolantPressureDisplay.setBrightness(brt);
    coolantPressureDisplay.setSegments(SEG_0THE);
    auxMessageDisplay.setBrightness(brt);
    auxMessageDisplay.setSegments(SEG_TIC0);
    delay(75);
  }

  oilPressureDisplay.setSegments(SEG_BLANK);
  coolantPressureDisplay.setSegments(SEG_BLANK);
  oilTemperatureDisplay.setSegments(SEG_BLANK);
  auxMessageDisplay.setSegments(SEG_BLANK);

  delay(1000);

  oilPressureDisplay.setBrightness(7);
  oilTemperatureDisplay.setBrightness(7);
  coolantPressureDisplay.setBrightness(7);
  auxMessageDisplay.setBrightness(7);
  oilPressureDisplay.showNumberDec(8888);
  coolantPressureDisplay.showNumberDec(8888);
  oilTemperatureDisplay.showNumberDec(8888);
  auxMessageDisplay.showNumberDec(8888);
  tachLights(0xffff);

  delay(500);

  oilPressureDisplay.setSegments(SEG_PSI);
  coolantPressureDisplay.setSegments(SEG_PSI);
  oilTemperatureDisplay.setSegments(SEG_TP);
  auxMessageDisplay.setSegments(SEG_RSSI);


  delay(500);

  oilPressureDisplay.setSegments(SEG_BLANK);
  coolantPressureDisplay.setSegments(SEG_BLANK);
  oilTemperatureDisplay.setSegments(SEG_BLANK);
  tachLights(0);
  #else
  oilPressureDisplay.setBrightness(7);
  oilTemperatureDisplay.setBrightness(7);
  coolantPressureDisplay.setBrightness(7);
  auxMessageDisplay.setBrightness(7);
  #endif

  attachInterrupt(digitalPinToInterrupt(TACH_SIGNAL_PIN), onTachPulseISR, RISING);
}

long lastSampleMillis = 0;
long lastRadioMillis = 0;

uint16_t rpm = 0;
double oilPressure = 0;
double coolantPressure = 0;
double oilTemperature = 0;
double batteryVoltage = 0;
bool idiotLight = false;

// Note: These are 32-bit because tach pulse lengths at low engine RPMs don't comfortably
// fit in a 16-bit microsecond value. They technically do, but barely.
// These values are volatile because they're accessed in an ISR.

// Time last saw a pulse.
volatile uint32_t microsAtLastTachPulse = 0;
// Time saw previous pulse.
volatile uint32_t microsAtPenultimateTachPulse = 0;

void onTachPulseISR() {
  uint32_t microsNow = micros(); // Call micros() asap so we don't include processor time.
  // Called whenever there's a rising edge on the tach pin.
  // Needs to run as fast as possible to prevent other interrupts
  // (such as the clock/delay()) from getting postponed.
  // Since doing 32-bit math is pretty slow on this micro,
  // we just write down some numbers and do the processing later.

  // Prevent interrupts because these are multibyte values, and are therefore
  // not atomically accessed.
  noInterrupts();
  microsAtPenultimateTachPulse = microsAtLastTachPulse;
  microsAtLastTachPulse = microsNow;
  interrupts();
}

void loop() {
  long millisNow = millis();
  if (millisNow - lastSampleMillis > SAMPLE_PERIOD_MS) {
    lastSampleMillis = millisNow;
    oilPressure = readOilPSI();
    coolantPressure = readCoolantPSI();
    oilTemperature = readOilTemp();
    batteryVoltage = readBatteryVoltage();
    rpm = readRpm();

    idiotLight = shouldShowIdiotLight();

    oilPressureDisplay.showNumberDec(oilPressure);
    coolantPressureDisplay.showNumberDec(coolantPressure);
    oilTemperatureDisplay.showNumberDec(oilTemperature);
  }

  if (millisNow - lastRadioMillis > RADIO_UPDATE_PERIOD_MS) {
    lastRadioMillis = millisNow;

    // TODO: If we stagger these, they won't block each other.
    // As it is, the last 2 messages end up blocking on the
    // previous message being sent, which takes ~hundreds of
    // ms each time. It would be nice for the main loop to have
    // a fairly consistent runtime i.e. for easy animations and
    // blinkenlichten.
    sendRadioMessage(RADIO_MSG_OIL_PRES, (uint16_t)oilPressure);
    sendRadioMessage(RADIO_MSG_COOLANT_PRES, (uint16_t)coolantPressure);
    sendRadioMessage(RADIO_MSG_OIL_TEMP, (uint16_t)oilTemperature);
    sendRadioMessage(RADIO_MSG_BATTERY_VOLTAGE, (uint16_t)(1000.0 * batteryVoltage));
    sendRadioMessage(RADIO_MSG_RPM, (uint16_t)rpm);
    sendRadioMessage(RADIO_MSG_FAULT, idiotLight);
  }

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

  if (!radioAvailable) {
    auxMessageDisplay.setSegments(millis() % 2000 > 1000 ? SEG_RDIO : SEG_INOP);
  }

  updateTach(rpm, idiotLight);
}

void sendRadioMessage(const char* msg, uint16_t data) {
  if (!radioAvailable) {
    return;
  }

  int bytesWritten = sprintf(radioMsgBuf, "%s:%05u\n", msg, data);

  Serial.print("Radio message:");
  Serial.print(radioMsgBuf);

  rf95.send((const unsigned char*)radioMsgBuf, bytesWritten);
  // Not necessary - send() will wait all by itself if needed.
  //rf95.waitPacketSent();
}

double readRpm() {
  double rpmNow;
  // Prevent interrupts because these are multibyte values, and are therefore
  // not atomically accessed.
  noInterrupts();
  uint32_t tachPeriodMicros = microsAtLastTachPulse - microsAtPenultimateTachPulse;
  interrupts();
  tachPeriodMicros -= TACH_ISR_OFFSET_MICROS;

  if (tachPeriodMicros > 0) {
    rpmNow =
      // rpm/hz
      60.0 *
      // pulse freq in hz
      (1e6 / (double)tachPeriodMicros) /
      // num cylinders
      4.0;
    // If the value is completely nonsensical, it's much more likely that the engine is off
    // or the circuitry went bad somehow vs. the engine spontaneously becoming a rotary.
    if (rpmNow > 10000) {
      rpmNow = 0;
    }
  } else {
    rpmNow = 0;
  }

  return rpmNow;
}

CircularBuffer<double,WINDOW_SIZE> batteryVoltageWindow;
double readBatteryVoltage() {
  // TODO: Calibrate ADC.
  double vbat =
    VBAT_RATIO * // resistor divider
    5.0 * // full scale of ADC (volts)
    (double)analogRead(VBAT_VIN_PIN) /
    1024.0; // full scale of ADC (counts)
  batteryVoltageWindow.push(vbat);
  return avg(batteryVoltageWindow);
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


bool shouldShowIdiotLight() {
  bool oilPressBad = oilPressure < 15;
  bool coolantPressBad = coolantPressure < 5;
  bool oilTempBad = oilTemperature > 240;
  bool vbatBad = batteryVoltage < VBAT_WARN_MIN || batteryVoltage > VBAT_WARN_MAX;
  return oilPressBad ||
    coolantPressBad ||
    oilTempBad ||
    vbatBad;
}

double avg(CircularBuffer<double,WINDOW_SIZE> &cb) {
  if (cb.size() == 0) return 0;
  double total = 0;
  for (int i = 0; i <= cb.size(); i++) {
    total += cb[i];
  }
  return total / cb.size();
}
