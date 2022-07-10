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
#define REDLINE_RPM 5700
#define FIRST_LIGHT_RPM 1000

/* Sensor sampling */
#define SAMPLE_PERIOD_MS 25

// The radio blocks the main loop if we try to send a message
// but the radio hasn't had a chance to send it yet. To avoid this,
// we send out each radio message type in sequence separated by a length
// of time configured here.
// To avoid blocking on the radio, set this to a value
// that pretty much guarantees we'll have sent the packet
// by the time we try to send again.
#define RADIO_UPDATE_PERIOD_MS 1000
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
#define OIL_TEMP_NOMINAL_RESISTANCE 50000.0
#define OIL_TEMP_NOMINAL_TEMP 25.0
#define OIL_TEMP_BETA_COEFFICIENT 3892.0

#define AUX_MESSAGE_CLK 22
#define AUX_MESSAGE_DIO 23

#define COOLANT_PRESSURE_CLK 24
#define COOLANT_PRESSURE_DIO 23
#define COOLANT_PRESSURE_VIN_PIN 2
#define COOLANT_TEMPERATURE_VIN_PIN 5

RH_RF95 rf95;
bool radioAvailable;

/*
|Code|English             |Unit                 |
|T_C |Coolant temperature |Tenths of a degree F |
|P_C |Coolant pressure    |Tenths of a PSI      |
|T_O |Oil temperature     |Tenths of a degree F |
|P_O |Oil pressure        |PSI                  |
|VBA |Battery voltage     |Millivolts           |
|RPM |RPM                 |RPM                  |
|SPD |GPS Speed           |Tenths of a MPH      |
|TIM |GPS Lap Time        |Deciseconds          |
|FLT |Fault               |Fault code, see below|
|MET |Mission Elapsed Time|Seconds              |
|PIT |Return To Pits!     |!0=Return Now 0=Race |

Fault code
Digit 0 is the least significant, 5 is the most significant.
|Digit|Meaning
|0    |Idiot light. 0: Light off 1: Light on    |
*/

const char* RADIO_MSG_COOLANT_PRES = "P_C";
const char* RADIO_MSG_COOLANT_TEMP = "T_C";
const char* RADIO_MSG_OIL_TEMP = "T_O";
const char* RADIO_MSG_OIL_PRES = "P_O";
const char* RADIO_MSG_FAULT = "FLT";
const char* RADIO_MSG_BATTERY_VOLTAGE = "VBA";
const char* RADIO_MSG_RPM = "RPM";
const char* RADIO_MSG_MET = "MET";
const char* RADIO_MSG_PIT = "PIT";


char radioMsgBuf[RH_RF95_MAX_MESSAGE_LEN];

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

const uint8_t SEG_PITS[] = {
  SEG_A | SEG_F | SEG_B | SEG_G | SEG_E,
  SEG_F | SEG_E,
  SEG_F | SEG_G | SEG_E,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D
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

long missionStartTimeMillis;
bool returnToPitsRequested;

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
  auxMessageDisplay.showNumberDec(0);
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
  missionStartTimeMillis = millis();
  returnToPitsRequested = false;
}

long lastSampleMillis = 0;
long lastRadioMillis = 0;

uint16_t rpm = 0;
double oilPressure = 0;
double coolantPressure = 0;
double coolantTemperature = 0;
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
    coolantTemperature = readCoolantTemperature();
    oilTemperature = readOilTemp();
    batteryVoltage = readBatteryVoltage();
    double rpmNow = readRpm();
    if (rpmNow >= 0) {
      // RPM value is nonsensical - either the engine is off,
      // there was an odd electrical pulse, or something else.
      // TODO: Timeout waiting for a tach pulse and set RPM
      // to zero. Nice to have.
      rpm = rpmNow;
    }

    idiotLight = shouldShowIdiotLight();

    oilPressureDisplay.showNumberDec(oilPressure);
    coolantPressureDisplay.showNumberDec(coolantPressure);
    oilTemperatureDisplay.showNumberDec(oilTemperature);

    uint16_t missionElapsedTimeS = (millis() - missionStartTimeMillis) / 1000;
    // Convert seconds elapsed to minute:second coded as decimal to send
    // to the display.
    uint16_t minutesSecondsCodedDecimal =
      (missionElapsedTimeS % 60) +
      (missionElapsedTimeS / 60) * 100;
    auxMessageDisplay.showNumberDec(minutesSecondsCodedDecimal, /*leading_zero*/ true);


    // Flash radio inop message every so often if it's broken.
    // Will obscure the timer, ah well.
    if (!radioAvailable && millis() % 2000 > 1000) {
      auxMessageDisplay.setSegments(SEG_INOP);
    }

    // Flash "PITS" in MET display if pitting is requested.
    if (returnToPitsRequested && millis() % 500 > 250) {
      auxMessageDisplay.setSegments(SEG_PITS);
    }
  }

  if (millisNow - lastRadioMillis > RADIO_UPDATE_PERIOD_MS) {
    lastRadioMillis = millisNow;
    sendTelemetryPacket();
  }

  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN + 1];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len)) {
      buf[len] = 0;
      Serial.println((char*)buf);
      // For now we only accept one command (RADIO_MSG_PIT). Make better when appropriate.
      if (strncmp(RADIO_MSG_PIT, buf, strlen(RADIO_MSG_PIT)) == 0) {
        int parsed;
        sscanf(buf, "PIT:%d", &parsed);
        returnToPitsRequested = parsed != 0;
      }
      Serial.print("RSSI: ");
      Serial.println(rf95.lastRssi(), DEC);
    } else {
      Serial.println("recv failed");
    }
  }

  updateTach(rpm, idiotLight);
}

// Send one packet containing all telemetry information, separated by newlines.
void sendTelemetryPacket() {
  if (!radioAvailable) {
    return;
  }

  int bytesWritten = sprintf(radioMsgBuf,
    "%s:%05u\n%s:%05u\n%s:%05u\n%s:%05u\n%s:%05u\n%s:%05u\n%s:%05u\n%s:%05u\n",
    RADIO_MSG_OIL_PRES, (uint16_t)oilPressure,
    RADIO_MSG_COOLANT_PRES, (uint16_t)(coolantPressure*10.0),
    RADIO_MSG_COOLANT_TEMP, (uint16_t)(coolantTemperature*10.0),
    RADIO_MSG_OIL_TEMP, (uint16_t)oilTemperature,
    RADIO_MSG_BATTERY_VOLTAGE, (uint16_t)(1000.0 * batteryVoltage),
    RADIO_MSG_RPM, (uint16_t)rpm,
    RADIO_MSG_FAULT, (uint16_t)idiotLight,
    RADIO_MSG_MET, (uint16_t)((millis() - missionStartTimeMillis) / 1000)
  );

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
      2.0 * // Intake/exhaust stroke 
      // rpm/hz
      60.0 *
      // pulse freq in hz
      (1e6 / (double)tachPeriodMicros) /
      // num cylinders
      4.0;
    // OK I hate this but we compared the result of this with the car and there's a pretty severe linear discrepancy so....
    // we're going to trust what the gauge says.
    rpmNow = 1.107 * rpmNow - 454;
    // If the value is completely nonsensical, it's much more likely that the engine is off
    // or the circuitry went bad somehow vs. the engine spontaneously becoming a rotary.
    // This can happen if there's a particularly noisy transient that double-triggers the
    // interrupt in a very quick succession.
    if (rpmNow > 9000 || rpmNow < 0) {
      rpmNow = -1; // Invalid
    }
  } else {
    rpmNow = -1; // Invalid
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
  // Transducer is resistive. Circuit in car:
  // (5V)--\/\/\/---(Vpsi)---\/\/\/---(GND)
  //       Rs=100     |       Rpsi
  //                 ADC0
  // Rs is a fixed resistor.
  // Rpsi is the transducer.
  //
  // From manufacturer of AutoMeter 2242 100psi transducer
  // PSI | Ohm | Expected Vpsi w/Rs = 100ohm
  //   1 | 250 | 3.57
  //  10 | 215 | 3.41
  //  25 | 158 | 3.06
  //  50 | 111 | 2.63
  //  75 |  75 | 2.14
  // 100 |  43 | 1.50
  //
  // This formula comes from "oil pressure transducer calibration.ods"
  double analogReading = analogRead(OIL_PRESSURE_VIN_PIN);
  double psi = -0.2379 * analogReading + 175.9278;
  if (psi < 0 || !isfinite(psi)) {
    psi = 0;
  }
  oilPSIWindow.push(psi);
  return avg(oilPSIWindow);
}

CircularBuffer<double,WINDOW_SIZE> coolantPSIWindow;
double readCoolantPSI() {
  //double psi = (((double)(analogRead(COOLANT_PRESSURE_VIN_PIN) - 122)) / 1024.0) * 80.0;
  double psi = map(analogRead(COOLANT_PRESSURE_VIN_PIN), 102, 930, 0, 80);
  coolantPSIWindow.push(psi);
  return avg(coolantPSIWindow);
}

CircularBuffer<double,WINDOW_SIZE> coolantTempWindow;
double readCoolantTemperature() {
  double analogReading = analogRead(COOLANT_TEMPERATURE_VIN_PIN);
  double temp = -0.000127 * analogReading * analogReading - 0.080997 * analogReading + 269.151692;
  coolantTempWindow.push(temp);
  return avg(coolantTempWindow);
}


// https://www.amazon.com/Universal-Water-Temperature-Sensor-Sender/dp/B0771KB6FN/ref=sr_1_17?dchild=1&keywords=oil+temperature+sensor&qid=1590294751&sr=8-17
CircularBuffer<double,WINDOW_SIZE> oilTempWindow;
double readOilTemp() {
  double vin = 5.0; // Supply voltage, output is ratiometric to this.
  
  // Min seen in practice (~75F day): ~180
  // Max seen in mild driving (~75F day): ~690
  double analogReadValue = analogRead(OIL_TEMP_PIN);
  
  double vout = (double)((analogReadValue * vin) / 1024.0);
  double ohms = (((OIL_TEMP_R2_K_OHM * 1000) * vin) / vout) - OIL_TEMP_R2_K_OHM;
  double tempC = 1 / ( ( log( ohms / OIL_TEMP_NOMINAL_RESISTANCE )) / OIL_TEMP_BETA_COEFFICIENT + 1 / ( OIL_TEMP_NOMINAL_TEMP + 273.15 ) ) - 273.15;
  double tempF = (tempC * 1.8) + 32;
  oilTempWindow.push(tempF);
  return avg(oilTempWindow);
}


bool shouldShowIdiotLight() {
  bool oilPressBad = oilPressure < 15;
  bool coolantPressBad = coolantPressure < 5;
  bool coolantTempBad = coolantTemperature > 220.0;
  bool oilTempBad = oilTemperature > 240;
  bool vbatBad = batteryVoltage < VBAT_WARN_MIN || batteryVoltage > VBAT_WARN_MAX;
  return oilPressBad ||
    coolantPressBad ||
    coolantTempBad ||
    oilTempBad ||
    vbatBad;
}

double avg(CircularBuffer<double,WINDOW_SIZE> &cb) {
  if (cb.size() == 0) return 0;
  double total = 0;
  for (int i = 0; i < cb.size(); i++) {
    total += cb[i];
  }
  return total / cb.size();
}
