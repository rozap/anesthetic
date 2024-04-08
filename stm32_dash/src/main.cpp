/*
  dance to update firmware:
    1 plug in USB
    2 Hold down the BOOT0 button then tap NRST. This will put the device into bootloader mode. You can now release both buttons.
    3 Upload. No rush, there isn't a timeout.
    4 Repeat from step 2 to upload firmware the next time. No need to unplug USB.
  wtf:
    * pin3 has to be ON to program the thing
      * press reset each time to program
    * pin3 has to be OFF to run program
    * need delay on powerup or else it doesn't work at all
    * println is over Serial1. The following works:
      void setup() {
        while (!Serial1);
        Serial1.begin(115200);
      }

      void loop() {
        // Serial1.println("Working");
        delay(500);
      }

*/

// Configuration

// Extra debugging for speeduino comms.
#define DEBUG_SPEEDUINO_COMMS

// Display the bootsplash (disable if debugging to shorten upload times).
#define BOOTSPLASH

// Use data in mock_pkt.h instead of actually reading from the serial port.
//#define USE_MOCK_DATA


#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <CircularBuffer.h>
#include "tach.h"

/* Radio */

// Some vestigial RadioHead stuff we can refactor out later.

// RadioHead header compat
#define RH_RF95_HEADER_LEN 4
#define RH_BROADCAST_ADDRESS 0xff

// Max number of octets the LORA Rx/Tx FIFO can hold
#define RH_RF95_FIFO_SIZE 255

// This is the maximum number of bytes that can be carried by the LORA.
// We use some for headers, keeping fewer for RadioHead messages
#define RH_RF95_MAX_PAYLOAD_LEN RH_RF95_FIFO_SIZE

// This is the maximum message length that can be supported by this driver.
// Can be pre-defined to a smaller size (to save SRAM) prior to including this header
// Here we allow for 1 byte message length, 4 bytes headers, user data and 2 bytes of FCS
#define RH_RF95_MAX_MESSAGE_LEN (RH_RF95_MAX_PAYLOAD_LEN - RH_RF95_HEADER_LEN)

#include <LoRa_STM32.h>

#ifdef BOOTSPLASH
#include "boot_image.h"
#endif

#ifdef USE_MOCK_DATA
#include "mock_pkt.h"
#endif

// Serial1: RX: A10, TX: A9. Debug and programming port.
HardwareSerial DebugSerial = Serial1;

// Serial2: RX: A3, TX: A2. Connected to Speeduino.
HardwareSerial SpeeduinoSerial = Serial2;

long missionStartTimeMillis;

#define WIDTH 320
#define HEIGHT 240

// Averaging window size for analog readings (oil temp and fuel level).
#define WINDOW_SIZE 16

#define FUEL_VIN 3.3
#define FUEL_ANALOG_PIN PB0
#define FUEL_REF_OHM 47

#define OIL_TEMP_VIN 3.3
#define OIL_TEMP_ANALOG_PIN PB1
#define OIL_TEMP_REF_OHM 47

#define BAR_HEIGHT 8
#define WUT ILI9341_CYAN

// 16 bit TFT, 5 bits red, 6 green, 5 blue
#define BACKGROUND_COLOR ILI9341_BLACK
#define BAR_COLOR ILI9341_CYAN
uint8_t timeouts = 0;
bool requestFrame = false;

#define SCREEN_STATE_NO_DATA 1
#define SCREEN_STATE_NO_CONNECTION 2
#define SCREEN_STATE_NORMAL 0
uint8_t screenState = SCREEN_STATE_NO_CONNECTION;
uint8_t lastScreenState = SCREEN_STATE_NO_CONNECTION;

#define LIMIT_COOLANT_UPPER 215
#define LIMIT_OIL_LOWER 10
#define LIMIT_FUEL_LOWER 10
#define LIMIT_RPM_UPPER 5500

// Render structs used to only re-render what we need (fps 4 -> ~30).

struct StatusMessages
{
  bool fanOff;
  bool fanOn;
  bool engHot;
  bool lowGas;
  bool lowOilPressure;
  bool overRev;
  bool allOk;
  bool engOff;
  bool running;
  bool cranking;
  bool warmup;
  bool ase;

  bool operator==(const StatusMessages&) const = default; // Auto == operator.
} statusMessages, lastRenderedStatusMessages;

struct SecondaryInfo
{
  double airFuel;
  int advance;
  int iat;
  double volts;
  int missionElapsedSeconds;

  bool operator==(const SecondaryInfo&) const = default; // Auto == operator.
} secondaryInfo, lastRenderedSecondaryInfo;

struct Colors
{
  int bar;
  int background;
  int text;
} okColors, errorColors;

void bumpTimeout()
{
  timeouts++;
  requestFrame = true;
  screenState = SCREEN_STATE_NO_DATA;
}
void resetTimeout()
{
  timeouts = 0;
  requestFrame = true;
}

bool isNthBitSet(unsigned char c, int n)
{
  // static unsigned char mask[] = {128, 64, 32, 16, 8, 4, 2, 1};
  static unsigned char mask[] = {1, 2, 4, 8, 16, 32, 64, 128};

  return ((c & mask[n]) != 0);
}

// Define bit positions within engine variable
#define BIT_ENGINE_RUN 0    // Engine running
#define BIT_ENGINE_CRANK 1  // Engine cranking
#define BIT_ENGINE_ASE 2    // after start enrichment (ASE)
#define BIT_ENGINE_WARMUP 3 // Engine in warmup
#define BIT_ENGINE_ACC 4    // in acceleration mode (TPS accel)
#define BIT_ENGINE_DCC 5    // in deceleration mode
#define BIT_ENGINE_MAPACC 6 // MAP acceleration mode
#define BIT_ENGINE_MAPDCC 7 // MAP deceleration mode

#define BIT_STATUS4_WMI_EMPTY 0  // Indicates whether the WMI tank is empty
#define BIT_STATUS4_VVT1_ERROR 1 // VVT1 cam angle within limits or not
#define BIT_STATUS4_VVT2_ERROR 2 // VVT2 cam angle within limits or not
#define BIT_STATUS4_FAN 3        // Fan Status
#define BIT_STATUS4_BURNPENDING 4
#define BIT_STATUS4_STAGING_ACTIVE 5
#define BIT_STATUS4_COMMS_COMPAT 6
#define BIT_STATUS4_ALLOW_LEGACY_COMMS 7

struct SpeeduinoStatus
{
  uint8_t secl;
  uint8_t status1;
  byte engine;
  uint8_t syncLossCounter;
  uint16_t MAP;
  uint8_t IAT;
  uint8_t coolant;
  uint8_t batCorrection;
  uint8_t battery10;
  uint8_t O2;
  uint8_t egoCorrection;
  uint8_t iatCorrection;
  uint8_t wueCorrection;
  uint16_t RPM;
  uint8_t TAEamount;
  uint8_t corrections;
  uint8_t ve;
  uint8_t afrTarget;
  uint16_t PW1;
  uint8_t tpsDOT;
  uint8_t advance;
  uint8_t TPS;
  uint16_t loopsPerSecond;
  uint16_t freeRAM;
  uint8_t boostTarget;
  uint8_t boostDuty;
  uint8_t spark;
  uint16_t rpmDOT;
  uint8_t ethanolPct;
  uint8_t flexCorrection;
  uint8_t flexIgnCorrection;
  uint8_t idleLoad;
  uint8_t testOutputs;
  uint8_t O2_2;
  uint8_t baro;
  uint16_t CANin_1;
  uint16_t CANin_2;
  uint16_t CANin_3;
  uint16_t CANin_4;
  uint16_t CANin_5;
  uint16_t CANin_6;
  uint16_t CANin_7;
  uint16_t CANin_8;
  uint16_t CANin_9;
  uint16_t CANin_10;
  uint16_t CANin_11;
  uint16_t CANin_12;
  uint16_t CANin_13;
  uint16_t CANin_14;
  uint16_t CANin_15;
  uint16_t CANin_16;
  uint8_t tpsADC;
  uint8_t getNextError;
  uint8_t launchCorrection;
  uint16_t PW2;
  uint16_t PW3;
  uint16_t PW4;
  uint8_t status3;
  uint8_t engineProtectStatus;
  uint8_t fuelLoad;
  uint8_t ignLoad;
  uint8_t injAngle;
  uint8_t idleDuty;
  uint8_t CLIdleTarget;
  uint8_t mapDOT;
  uint8_t vvt1Angle;
  uint8_t vvt1TargetAngle;
  uint8_t vvt1Duty;
  uint8_t flexBoostCorrection;
  uint8_t baroCorrection;
  uint8_t ASEValue;
  uint8_t vss;
  uint8_t gear;
  uint8_t fuelPressure;
  uint8_t oilPressure;
  uint8_t wmiPW;
  uint8_t status4;
  uint8_t vvt2Angle;
  uint8_t vvt2TargetAngle;
  uint8_t vvt2Duty;
  uint8_t outputsStatus;
  uint8_t fuelTemp;
  uint8_t fuelTempCorrection;
  uint8_t VE1;
  uint8_t VE2;
  uint8_t advance1;
  uint8_t advance2;
  uint8_t nitrous_status;
  uint8_t TS_SD_Status;
  uint8_t fanDuty;

};
SpeeduinoStatus speeduinoSensors;

/*
 * State of sensors serviced directly by this MCU.
 */
struct LocalSensors
{
  double oilTemp;
  int fuelPct;
  uint16_t missionElapsedSeconds;

} localSensors;

// Internal data for sensor sliding avg.
CircularBuffer<double, WINDOW_SIZE> oilTempWindow;
CircularBuffer<double, WINDOW_SIZE> fuelWindow;

// Telemetry is sent at most this often.
#define TELEMETRY_UPDATE_PERIOD_MS 1000
long lastTelemetryPacketSentAtMillis;

TFT_eSPI tft = TFT_eSPI();

// SPI port 2
//                COPI   CIPO   SCLK  PSEL
SPIClass radioSPI(PB15, PB14, PB13); //, PB12);
bool radioAvailable;
bool radioBusy;

/*
|Code|English                 |Unit                 | Notes
|T_C |Coolant temperature     |Tenths of a degree F |
|P_C |Coolant pressure        |Tenths of a PSI      |
|T_O |Oil temperature         |Tenths of a degree F |
|P_O |Oil pressure            |PSI                  |
|VBA |Battery voltage         |Millivolts           |
|RPM |RPM                     |RPM                  |
|SPD |GPS Speed               |Tenths of a MPH      |
|TIM |GPS Lap Time            |Deciseconds          |
|FLT |Fault                   |Fault code, see below|
|MET |Mission Elapsed Time    |Seconds              |
|PIT |Return To Pits!         |!0=Return Now 0=Race |
|GAS |Fuel remaining.         |Percent              |
|S_E |Speeduino engine status |Bitfield             |https://github.com/noisymime/speeduino/blob/df78f5109c86cd2a1e9314138959738d1a33f039/speeduino/globals.h#L136
|ADV |Engine advance          |Degrees              |
|O_2 |O2 sensor reading       |Umm.... oxygens?     |
|IAT |Intake air temperature  |Umm....              |
|SLS |Sync loss counter       |Count                |
|MAP |Manifold absolute press |Umm.... psi?         |
|V_E |Volumetric efficiency   |???                  |
|AFT |AFR target              |Ratio                |
|TPS |Throttle position       |???                  |
|S_P |Speeduino engine protect|Bitfield             |https://github.com/noisymime/speeduino/blob/df78f5109c86cd2a1e9314138959738d1a33f039/speeduino/globals.h#L136
|FAN |Fan duty cycle          |Percent... I think   |
|S_1 |Speeduino status1       |Bitfield             |https://github.com/noisymime/speeduino/blob/df78f5109c86cd2a1e9314138959738d1a33f039/speeduino/globals.h#L146
|S_3 |Speeduino status3       |Bitfield             |https://github.com/noisymime/speeduino/blob/df78f5109c86cd2a1e9314138959738d1a33f039/speeduino/globals.h#L182
|S_4 |Speeduino status4       |Bitfield             |https://github.com/noisymime/speeduino/blob/df78f5109c86cd2a1e9314138959738d1a33f039/speeduino/globals.h#L191

Fault code
Digit 0 is the least significant, 5 is the most significant.
|Digit|Meaning
|0    |Idiot light. 0: Light off 1: Light on    |
*/

const char *RADIO_MSG_COOLANT_PRES = "P_C";
const char *RADIO_MSG_COOLANT_TEMP = "T_C";
const char *RADIO_MSG_OIL_TEMP = "T_O";
const char *RADIO_MSG_OIL_PRES = "P_O";
const char *RADIO_MSG_FAULT = "FLT";
const char *RADIO_MSG_BATTERY_VOLTAGE = "VBA";
const char *RADIO_MSG_RPM = "RPM";
const char *RADIO_MSG_MET = "MET"; // Mission elapsed time
const char *RADIO_MSG_PIT = "PIT";
const char *RADIO_MSG_ACK = "ACK"; // Acknowledge current issues
const char *RADIO_MSG_NAK = "NAK"; // Un-acknowledge current issues
const char *RADIO_MSG_GPS = "GPS";
const char *RADIO_MSG_SPEED = "SPD";
const char *RADIO_MSG_FUEL_PCT = "GAS";
const char *RADIO_MSG_SPEEDUINO_ENGINE_STATUS = "S_E";
const char *RADIO_MSG_ADVANCE = "ADV";
const char *RADIO_MSG_O2 = "O_2";
const char *RADIO_MSG_IAT = "IAT";
const char *RADIO_MSG_SYNC_LOSS_COUNTER = "SLS";
const char *RADIO_MSG_MAP = "MAP";
const char *RADIO_MSG_VE = "V_E";
const char *RADIO_MSG_AFR_TARGET = "AFT";
const char *RADIO_MSG_TPS = "TPS";
const char *RADIO_MSG_SPEEDUINO_PROTECT_STATUS = "S_P";
const char *RADIO_MSG_FAN_DUTY = "FAN";
const char *RADIO_MSG_SPEEDUINO_STATUS_1 = "S_1";
const char *RADIO_MSG_SPEEDUINO_STATUS_3 = "S_3"; // Don't ask me why there isn't a status2.
const char *RADIO_MSG_SPEEDUINO_STATUS_4 = "S_4";

// Note! These values are only for the main render() method.
long fpsCounterStartTime;
uint8_t frameCounter; // Accumulates # render frames, resets every 50 frames.

// Helpful constants for graphics code.
uint16_t fontHeightSize2;
uint16_t charWidthSize2;

double avg(CircularBuffer<double, WINDOW_SIZE> &cb)
{
  if (cb.size() == 0)
    return 0;
  double total = 0;
  for (int i = 0; i <= cb.size(); i++)
  {
    total += cb[i];
  }
  return total / cb.size();
}

void clearLine()
{
  tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.width(), tft.getCursorY() + tft.textsize, BACKGROUND_COLOR);
}

void drawPlainGauge(int value, int min, int max, int color)
{
  int width = map(value, min, max, 0, tft.width());

  tft.fillRect(width, tft.getCursorY(), tft.width() - width, BAR_HEIGHT, BACKGROUND_COLOR);

  // WIDTH / (max - min)
  tft.fillRect(
      0, tft.getCursorY(),
      width, BAR_HEIGHT,
      color);
  tft.println();
}

// Call this once per update interval.
// Consumers should look at localSensors.fuelPct.
void updateFuel()
{
  double vout = (double)((analogRead(FUEL_ANALOG_PIN) * FUEL_VIN) / 1024.0);
  double ohms = FUEL_REF_OHM * (vout / (FUEL_VIN - vout));
  fuelWindow.push(ohms);
  double avgOhms = avg(fuelWindow);

  double gallons = 29.0207 + (-7.0567 * log(avgOhms));
  int pct = floor((gallons / 14) * 100);
  if (pct < 0)
  {
    pct = 0;
  }

  #ifdef USE_MOCK_DATA
  pct = sin(millis() / 1000.0) * 50.0 + 50.0;
  #endif

  localSensors.fuelPct = pct;
}

// Call this once per update interval.
// Consumers should look at localSensors.oilTemp.
void updateOilT()
{
  double vout = (double)((analogRead(OIL_TEMP_ANALOG_PIN) * OIL_TEMP_VIN) / 1024.0);
  double ohms = OIL_TEMP_REF_OHM * (vout / (OIL_TEMP_VIN - vout));
  oilTempWindow.push(ohms);
  double avgOhms = avg(oilTempWindow);

  #ifdef USE_MOCK_DATA
  avgOhms = 50;
  #endif

  localSensors.oilTemp = avgOhms; // TODO calibrate.
}

void updateSecondaryInfo()
{
  secondaryInfo.airFuel = ((double)speeduinoSensors.O2) / 10.0;
  secondaryInfo.advance = speeduinoSensors.advance;
  secondaryInfo.iat = speeduinoSensors.IAT;
  secondaryInfo.volts = ((double)speeduinoSensors.battery10) / 10.0;
  secondaryInfo.missionElapsedSeconds = localSensors.missionElapsedSeconds;
}

void updateStatusMessages()
{
  statusMessages.fanOn = isNthBitSet(speeduinoSensors.status4, BIT_STATUS4_FAN);
  statusMessages.fanOff = !statusMessages.fanOn;
  statusMessages.engHot = speeduinoSensors.coolant > LIMIT_COOLANT_UPPER;
  statusMessages.lowGas = localSensors.fuelPct < LIMIT_FUEL_LOWER;
  statusMessages.lowOilPressure = speeduinoSensors.oilPressure < LIMIT_OIL_LOWER;
  statusMessages.overRev = speeduinoSensors.RPM > LIMIT_RPM_UPPER;
  statusMessages.running = isNthBitSet(speeduinoSensors.engine, BIT_ENGINE_RUN);
  statusMessages.cranking = isNthBitSet(speeduinoSensors.engine, BIT_ENGINE_CRANK);
  statusMessages.warmup = isNthBitSet(speeduinoSensors.engine, BIT_ENGINE_WARMUP);
  statusMessages.ase = isNthBitSet(speeduinoSensors.engine, BIT_ENGINE_ASE);
  statusMessages.engOff = !statusMessages.running && !statusMessages.cranking;

  statusMessages.allOk = !(
    statusMessages.engHot ||
    statusMessages.lowGas ||
    statusMessages.lowOilPressure ||
    statusMessages.overRev
  );
}

void moveToHalfWidth()
{
  tft.setCursor(WIDTH / 2 + 8, tft.getCursorY());
  clearLine();
}

void writeSingleStatusMessage(bool enabled, const char* msg)
{
  if (enabled) {
    moveToHalfWidth();
    tft.println(msg);
  }
}

void renderStatusMessages(int bottomPanelY)
{
  if (statusMessages == lastRenderedStatusMessages) {
    return;
  }

  lastRenderedStatusMessages = statusMessages;

  tft.setCursor(WIDTH / 2 + 8, bottomPanelY);
  tft.fillRect(WIDTH / 2 + 1, bottomPanelY - 4, WIDTH, HEIGHT, BACKGROUND_COLOR);

  tft.setTextColor(okColors.text);
  writeSingleStatusMessage(statusMessages.fanOn, "Fan On");
  writeSingleStatusMessage(statusMessages.fanOff, "Fan Off");

  tft.setTextColor(errorColors.text);
  writeSingleStatusMessage(statusMessages.engHot, "Eng Hot!");
  writeSingleStatusMessage(statusMessages.lowGas, "Low Gas!");
  writeSingleStatusMessage(statusMessages.lowOilPressure, "Low Oil Prs!");
  writeSingleStatusMessage(statusMessages.overRev, "Over rev!");
  writeSingleStatusMessage(statusMessages.engOff, "Eng Off");

  tft.setTextColor(okColors.text);
  writeSingleStatusMessage(statusMessages.allOk, "All OK");
  writeSingleStatusMessage(statusMessages.running, "Running");
  writeSingleStatusMessage(statusMessages.cranking, "Cranking");
  writeSingleStatusMessage(statusMessages.warmup, "Warmup");
  writeSingleStatusMessage(statusMessages.ase, "ASE");
}

void renderSecondaries(bool firstRender, int bottomPanelY)
{
  tft.setTextColor(ILI9341_WHITE, BACKGROUND_COLOR);

  uint16_t numberXPos = 96;
  uint16_t numberYPos = 134;

  if (firstRender) {
    tft.println("A/F");
    tft.println("TIMING");
    tft.println("IAT");
    tft.println("VOLTS");
    tft.println("STINT");
  }

  if (lastRenderedSecondaryInfo.airFuel != secondaryInfo.airFuel) {
    tft.setCursor(numberXPos, numberYPos);
    tft.printf(
      "% 2.2f",
      secondaryInfo.airFuel
    );
  }

  if (lastRenderedSecondaryInfo.advance != secondaryInfo.advance) {
    tft.setCursor(numberXPos + charWidthSize2, numberYPos + fontHeightSize2);
    tft.printf("% 3d", secondaryInfo.advance);
  }

  if (lastRenderedSecondaryInfo.iat != secondaryInfo.iat) {
    tft.setCursor(numberXPos + charWidthSize2, numberYPos + fontHeightSize2 * 2);
    tft.printf("% 4d", secondaryInfo.iat);
  }

  if (lastRenderedSecondaryInfo.volts != secondaryInfo.volts) {
    tft.setCursor(numberXPos, numberYPos + fontHeightSize2 * 3);
    tft.printf("% 2.1f", secondaryInfo.volts);
  }

  if (lastRenderedSecondaryInfo.missionElapsedSeconds != secondaryInfo.missionElapsedSeconds) {
    tft.setCursor(numberXPos, numberYPos + fontHeightSize2 * 4);
    tft.printf(
      "%02u:%02u",
      secondaryInfo.missionElapsedSeconds/60,
      secondaryInfo.missionElapsedSeconds%60
    );
  }

  lastRenderedSecondaryInfo = secondaryInfo;
}

// NOTE: numFmt should return a fixed-length string. This allows us to not worry about clearing
// old values from the screen - this cuts frame times by like a third.
void drawLabeledGauge(
  bool firstRender,
  const char *label,
  const char* numberFormat,
  int value,
  int min,
  int max,
  Colors colors)
{
  int numberXPos = charWidthSize2 * strlen(label);

  tft.setTextSize(2);
  tft.setTextColor(colors.text, colors.background);

  if (firstRender) {
    tft.print(label);
  }

  tft.setCursor(numberXPos, tft.getCursorY());
  tft.printf(
    numberFormat,
    value
  );
  tft.println();

  drawPlainGauge(value, min, max, colors.bar);
}

#define RESPONSE_LEN 128
uint8_t speeduinoResponse[RESPONSE_LEN];

void processResponse()
{

  speeduinoSensors.secl = speeduinoResponse[0];
  speeduinoSensors.status1 = speeduinoResponse[1];
  speeduinoSensors.engine = speeduinoResponse[2];
  speeduinoSensors.syncLossCounter = speeduinoResponse[3];
  speeduinoSensors.MAP = ((speeduinoResponse[5] << 8) | (speeduinoResponse[4]));
  speeduinoSensors.IAT = speeduinoResponse[6];
  speeduinoSensors.coolant = speeduinoResponse[7] - 40;
  speeduinoSensors.batCorrection = speeduinoResponse[8];
  speeduinoSensors.battery10 = speeduinoResponse[9];
  speeduinoSensors.O2 = speeduinoResponse[10];
  speeduinoSensors.egoCorrection = speeduinoResponse[11];
  speeduinoSensors.iatCorrection = speeduinoResponse[12];
  speeduinoSensors.wueCorrection = speeduinoResponse[13];

  speeduinoSensors.RPM = ((speeduinoResponse[15] << 8) | (speeduinoResponse[14]));

  speeduinoSensors.TAEamount = speeduinoResponse[16];
  speeduinoSensors.corrections = speeduinoResponse[17];
  speeduinoSensors.ve = speeduinoResponse[18];
  speeduinoSensors.afrTarget = speeduinoResponse[19];

  speeduinoSensors.PW1 = ((speeduinoResponse[21] << 8) | (speeduinoResponse[20]));

  speeduinoSensors.tpsDOT = speeduinoResponse[22];
  speeduinoSensors.advance = speeduinoResponse[23];
  speeduinoSensors.TPS = speeduinoResponse[24];

  speeduinoSensors.loopsPerSecond = ((speeduinoResponse[26] << 8) | (speeduinoResponse[25]));
  speeduinoSensors.freeRAM = ((speeduinoResponse[28] << 8) | (speeduinoResponse[27]));

  speeduinoSensors.boostTarget = speeduinoResponse[29];
  speeduinoSensors.boostDuty = speeduinoResponse[30];
  speeduinoSensors.spark = speeduinoResponse[31];

  speeduinoSensors.rpmDOT = ((speeduinoResponse[33] << 8) | (speeduinoResponse[32]));
  speeduinoSensors.ethanolPct = speeduinoResponse[34];

  speeduinoSensors.flexCorrection = speeduinoResponse[35];
  speeduinoSensors.flexIgnCorrection = speeduinoResponse[36];
  speeduinoSensors.idleLoad = speeduinoResponse[37];
  speeduinoSensors.testOutputs = speeduinoResponse[38];
  speeduinoSensors.O2_2 = speeduinoResponse[39];
  speeduinoSensors.baro = speeduinoResponse[40];
  speeduinoSensors.CANin_1 = ((speeduinoResponse[42] << 8) | (speeduinoResponse[41]));
  speeduinoSensors.CANin_2 = ((speeduinoResponse[44] << 8) | (speeduinoResponse[43]));
  speeduinoSensors.CANin_3 = ((speeduinoResponse[46] << 8) | (speeduinoResponse[45]));
  speeduinoSensors.CANin_4 = ((speeduinoResponse[48] << 8) | (speeduinoResponse[47]));
  speeduinoSensors.CANin_5 = ((speeduinoResponse[50] << 8) | (speeduinoResponse[49]));
  speeduinoSensors.CANin_6 = ((speeduinoResponse[52] << 8) | (speeduinoResponse[51]));
  speeduinoSensors.CANin_7 = ((speeduinoResponse[54] << 8) | (speeduinoResponse[53]));
  speeduinoSensors.CANin_8 = ((speeduinoResponse[56] << 8) | (speeduinoResponse[55]));
  speeduinoSensors.CANin_9 = ((speeduinoResponse[58] << 8) | (speeduinoResponse[57]));
  speeduinoSensors.CANin_10 = ((speeduinoResponse[60] << 8) | (speeduinoResponse[59]));
  speeduinoSensors.CANin_11 = ((speeduinoResponse[62] << 8) | (speeduinoResponse[61]));
  speeduinoSensors.CANin_12 = ((speeduinoResponse[64] << 8) | (speeduinoResponse[63]));
  speeduinoSensors.CANin_13 = ((speeduinoResponse[66] << 8) | (speeduinoResponse[65]));
  speeduinoSensors.CANin_14 = ((speeduinoResponse[68] << 8) | (speeduinoResponse[67]));
  speeduinoSensors.CANin_15 = ((speeduinoResponse[70] << 8) | (speeduinoResponse[69]));
  speeduinoSensors.CANin_16 = ((speeduinoResponse[72] << 8) | (speeduinoResponse[71]));
  speeduinoSensors.tpsADC = speeduinoResponse[73];
  speeduinoSensors.getNextError = speeduinoResponse[74];
  speeduinoSensors.launchCorrection = speeduinoResponse[75];
  speeduinoSensors.PW2 = ((speeduinoResponse[77] << 8) | (speeduinoResponse[76]));
  speeduinoSensors.PW3 = ((speeduinoResponse[79] << 8) | (speeduinoResponse[78]));
  speeduinoSensors.PW4 = ((speeduinoResponse[81] << 8) | (speeduinoResponse[80]));
  speeduinoSensors.status3 = speeduinoResponse[82];
  speeduinoSensors.engineProtectStatus = speeduinoResponse[83];
  speeduinoSensors.fuelLoad = ((speeduinoResponse[85] << 8) | (speeduinoResponse[84]));
  speeduinoSensors.ignLoad = ((speeduinoResponse[87] << 8) | (speeduinoResponse[86]));
  speeduinoSensors.injAngle = ((speeduinoResponse[89] << 8) | (speeduinoResponse[88]));
  speeduinoSensors.idleDuty = speeduinoResponse[90];
  speeduinoSensors.CLIdleTarget = speeduinoResponse[91];
  speeduinoSensors.mapDOT = speeduinoResponse[92];
  speeduinoSensors.vvt1Angle = speeduinoResponse[93];
  speeduinoSensors.vvt1TargetAngle = speeduinoResponse[94];
  speeduinoSensors.vvt1Duty = speeduinoResponse[95];
  speeduinoSensors.flexBoostCorrection = ((speeduinoResponse[97] << 8) | (speeduinoResponse[96]));
  speeduinoSensors.baroCorrection = speeduinoResponse[98];
  speeduinoSensors.ASEValue = speeduinoResponse[99];
  speeduinoSensors.vss = ((speeduinoResponse[101] << 8) | (speeduinoResponse[100]));
  speeduinoSensors.gear = speeduinoResponse[102];
  speeduinoSensors.fuelPressure = speeduinoResponse[103];
  speeduinoSensors.oilPressure = speeduinoResponse[104];
  speeduinoSensors.wmiPW = speeduinoResponse[105];
  speeduinoSensors.status4 = speeduinoResponse[106];
  // speeduinoSensors.vvt2Angle = speeduinoResponse[107];
  // speeduinoSensors.vvt2TargetAngle = speeduinoResponse[108];
  // speeduinoSensors.vvt2Duty = speeduinoResponse[109];
  // speeduinoSensors.outputsStatus = speeduinoResponse[110];
  // speeduinoSensors.fuelTemp = speeduinoResponse[111];
  // speeduinoSensors.fuelTempCorrection = speeduinoResponse[112];
  // speeduinoSensors.VE1 = speeduinoResponse[113];
  // speeduinoSensors.VE2 = speeduinoResponse[114];
  // speeduinoSensors.advance1 = speeduinoResponse[115];
  // speeduinoSensors.advance2 = speeduinoResponse[116];
  // speeduinoSensors.nitrous_status = speeduinoResponse[117];
  // speeduinoSensors.TS_SD_Status = speeduinoResponse[118];
  // speeduinoSensors.fanDuty = speeduinoResponse[121];
}

/**
 * Discards all data in Serial2's input buffer.
*/
void clearRx()
{
  while (SpeeduinoSerial.available() > 0)
  {
    SpeeduinoSerial.read();
  }
}

/**
 * Wait for the Speeduino to respond with a packet header, which is:
 * 0x6E ('n')
 * 0x32 ('2')
 * ...followed by one length byte (0-255).
 * 
 * The packet length is returned if a packet header is found within
 * timeout (500ms), else -1 is returned.
*/
int popHeader()
{
  #ifdef USE_MOCK_DATA
  return ___single_speeduino_pkt_bin[2];
  #else
  SpeeduinoSerial.setTimeout(500);
  if (SpeeduinoSerial.find('n'))
  {
    DebugSerial.println("Found N");

    if (SpeeduinoSerial.find(0x32))
    {
      while (!SpeeduinoSerial.available())
        ;
      DebugSerial.println("Return packetlen");
      return SpeeduinoSerial.read();
    }
  }
  return -1;
  #endif
}

void requestSpeeduinoUpdate()
{

  // clearRx();
  SpeeduinoSerial.write("n"); // Send n to request real time data
  #ifdef DEBUG_SPEEDUINO_COMMS
  DebugSerial.println("requested data");
  #endif

  int nLength = popHeader();

  if (nLength >= RESPONSE_LEN)
  {
    #ifdef DEBUG_SPEEDUINO_COMMS
    DebugSerial.println("Response pkt bigger than rec'v buf");
    DebugSerial.print("nLength=");
    DebugSerial.println(nLength);
    #endif
    clearRx();
  }
  else if (nLength > 0)
  {
    #ifdef DEBUG_SPEEDUINO_COMMS
    DebugSerial.print("nLength=");
    DebugSerial.println(nLength);
    #endif

    #ifdef USE_MOCK_DATA
    uint8_t nRead = ___single_speeduino_pkt_bin[2];
    memcpy(speeduinoResponse, ___single_speeduino_pkt_bin + 3, ___single_speeduino_pkt_bin_len - 3);
    #else
    uint8_t nRead = SpeeduinoSerial.readBytes(speeduinoResponse, nLength);
    #endif

    #ifdef DEBUG_SPEEDUINO_COMMS
    DebugSerial.print("nRead=");
    DebugSerial.println(nRead);
    #endif

    if (nRead < nLength)
    {
      #ifdef DEBUG_SPEEDUINO_COMMS
      DebugSerial.println("nRead < nLength");
      #endif
      bumpTimeout();
    }
    else
    {
      screenState = SCREEN_STATE_NORMAL;
      resetTimeout();
      processResponse();
      requestFrame = true;
    }
  }
  else
  {
    #ifdef DEBUG_SPEEDUINO_COMMS
    DebugSerial.println("popHeader -1");
    #endif
    bumpTimeout();
  }
}

// Called once per frame.
void updateAllSensors()
{
  requestSpeeduinoUpdate();
  updateFuel();
  updateOilT();
  localSensors.missionElapsedSeconds = (millis() - missionStartTimeMillis) / 1000;
}

void clearScreen()
{
  tft.fillScreen(ILI9341_BLACK);
}

void renderNoConnection()
{
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setCursor(0, 0);
  tft.println("No Connection");
}

void renderWarning()
{
  clearScreen();
  int padding = 10;

  tft.fillRect(padding, padding, WIDTH - padding, HEIGHT - padding, ILI9341_RED);

  tft.setCursor(padding * 2, padding * 2);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(3);
  tft.println(
      "WARNING");
  tft.setTextSize(2);
  tft.setCursor(tft.getCursorX() + (padding * 2), tft.getCursorY());

  tft.println("DANGER TO MANIFOLD!");
}

void renderNoData()
{
  //
  // renderWarning();
  // return;

  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(4);
  tft.setCursor(0, 0);
  clearLine();
  tft.println("No Data");
  
  tft.setTextSize(2);
  clearLine();
  tft.print("Timeouts ");
  tft.print(timeouts);
  tft.println("  ");

  clearLine();
  tft.print("Fuel ");
  tft.print(localSensors.fuelPct);
  tft.println("%   ");
}

void render(bool firstRender)
{
  tft.setTextSize(3);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_CYAN);

  int fuel = localSensors.fuelPct;
  drawLabeledGauge(firstRender, "FUEL   ", "% 3d", fuel, 0, 100, fuel < LIMIT_FUEL_LOWER ? errorColors : okColors);
  drawLabeledGauge(firstRender, "RPM  ", "% d", speeduinoSensors.RPM, 500, 7000, speeduinoSensors.RPM > LIMIT_RPM_UPPER ? errorColors : okColors);

  int coolantF = (int)(((float)speeduinoSensors.coolant) * 1.8 + 32);
  drawLabeledGauge(firstRender, "COOLANT", "% 3d", coolantF, 50, 250, coolantF > LIMIT_COOLANT_UPPER ? errorColors : okColors);
  drawLabeledGauge(firstRender, "OIL    ", "% 2d", speeduinoSensors.oilPressure, 0, 60, speeduinoSensors.oilPressure < LIMIT_OIL_LOWER ? errorColors : okColors);

  int bottomPanelY = tft.getCursorY();
  if (firstRender) {
    tft.drawLine(WIDTH / 2, bottomPanelY, WIDTH / 2, HEIGHT, ILI9341_WHITE);
    tft.drawLine(0, bottomPanelY, WIDTH, bottomPanelY, ILI9341_WHITE);
  }

  tft.setTextSize(2);

  bottomPanelY = bottomPanelY + 6;
  tft.setCursor(0, bottomPanelY);
  renderSecondaries(firstRender, bottomPanelY);
  renderStatusMessages(bottomPanelY);

  requestFrame = false;

  if (frameCounter >= 50) {
    double secondsForFrames = (millis() - fpsCounterStartTime) / 1000.0;
    fpsCounterStartTime = millis();
    DebugSerial.print("render FPS: ");
    DebugSerial.println(frameCounter / secondsForFrames);
    frameCounter = 0;
  } else {
    frameCounter ++;
  }
}

#ifdef BOOTSPLASH
void renderBootImage()
{
  uint32_t i = 0;
  for (uint16_t y = 0; y < boot_image_height; y++) {
    for (uint16_t x = 0; x < boot_image_width; x++) {
      tft.drawPixel(x, y, boot_image_data[i]);
      i++;
    }
  }
}
#endif

void loraSendDummyRadioHeadHeader()
{
  // Stub implementation of the header the RadioHead library uses.
  // It conveys source and destination addresses as well as extra
  // packet flags. We don't need these in our dash implementation,
  // but since the base station uses RadioHead let's just make it
  // happy. Perhaps we should consolidate libraries in the future.
  // NB: If we ever need to _read_ incoming packets, discard the
  // first 4 incoming bytes.
  LoRa.write(RH_BROADCAST_ADDRESS); // Header: TO
  LoRa.write(RH_BROADCAST_ADDRESS); // Header: FROM
  LoRa.write(0);                    // Header: ID
  LoRa.write(0);                    // Header: FLAGS
}

// Sends a single numeric value (sensor reading) over LoRa.
// I.e. "RPM:01000"
// Needs to happen after a beginPacket and after loraSendDummyRadioHeadHeader.
void loraSendNumericValue(const char * id, uint16_t value)
{
  char radioMsgBuf[12];
  int bytesWritten = snprintf(
    radioMsgBuf,
    RH_RF95_MAX_MESSAGE_LEN,
    "%s:%05u",
    id,
    value
  );

  if (bytesWritten < 0 || bytesWritten >= 12)
  {
    // Unlikely to happen unless someone sends us a long or corrupted id.
    DebugSerial.print("ERROR: Telemetry out of radio packet space. Check ID: ");
    DebugSerial.println(id);
  } else {
    LoRa.println(radioMsgBuf);
  }
}

// Send one packet containing all telemetry information, separated by newlines.
// May skip transmission if the radio is already busy or unavailable,
// in this case it returns 0. Else, returns 1.
int loraSendTelemetryPacket()
{
  if (!radioAvailable)
  {
    return 0;
  }

  if (radioBusy) {
    if (LoRa.isAsyncTxDone()) {
      radioBusy = false;
    } else {
      return 0;
    }
  }

  radioBusy = true;

  LoRa.beginPacket();
  loraSendDummyRadioHeadHeader();

  uint16_t checkEngineLight = 0; // TODO
  uint16_t coolantPressure = 0; // TODO

  // Each value takes 10 bytes:
  //   3 chars for ID
  //   2 chars for : and \n
  //   5 chars for value
  // That leaves us with ~25 (RH_RF95_MAX_MESSAGE_LEN/10) max # sensors in one packet.
  // Of course, we can send multiple packets if needed.
  // TODO: GPS throws this off, update this math once re-added.
  loraSendNumericValue(RADIO_MSG_OIL_PRES, speeduinoSensors.oilPressure);
  loraSendNumericValue(RADIO_MSG_COOLANT_PRES, coolantPressure * 10.0);
  loraSendNumericValue(RADIO_MSG_COOLANT_TEMP, speeduinoSensors.coolant * 10.0);
  loraSendNumericValue(RADIO_MSG_OIL_TEMP, localSensors.oilTemp * 10.0);
  loraSendNumericValue(RADIO_MSG_BATTERY_VOLTAGE, speeduinoSensors.battery10 * 100.0);
  loraSendNumericValue(RADIO_MSG_RPM, speeduinoSensors.RPM);
  loraSendNumericValue(RADIO_MSG_FAULT, checkEngineLight);
  loraSendNumericValue(RADIO_MSG_MET, localSensors.missionElapsedSeconds);

  loraSendNumericValue(RADIO_MSG_FUEL_PCT, localSensors.fuelPct);
  loraSendNumericValue(RADIO_MSG_SPEEDUINO_ENGINE_STATUS, speeduinoSensors.engine);
  loraSendNumericValue(RADIO_MSG_ADVANCE, speeduinoSensors.advance);
  loraSendNumericValue(RADIO_MSG_O2, speeduinoSensors.O2);
  loraSendNumericValue(RADIO_MSG_IAT, speeduinoSensors.IAT);
  loraSendNumericValue(RADIO_MSG_SYNC_LOSS_COUNTER, speeduinoSensors.syncLossCounter);

  loraSendNumericValue(RADIO_MSG_MAP, speeduinoSensors.MAP);
  loraSendNumericValue(RADIO_MSG_VE, speeduinoSensors.ve);
  loraSendNumericValue(RADIO_MSG_AFR_TARGET, speeduinoSensors.afrTarget);
  loraSendNumericValue(RADIO_MSG_TPS, speeduinoSensors.TPS);
  loraSendNumericValue(RADIO_MSG_SPEEDUINO_PROTECT_STATUS, speeduinoSensors.engineProtectStatus);
  loraSendNumericValue(RADIO_MSG_FAN_DUTY, speeduinoSensors.fanDuty);
  loraSendNumericValue(RADIO_MSG_SPEEDUINO_STATUS_1, speeduinoSensors.status1);
  loraSendNumericValue(RADIO_MSG_SPEEDUINO_STATUS_3, speeduinoSensors.status3);
  loraSendNumericValue(RADIO_MSG_SPEEDUINO_STATUS_4, speeduinoSensors.status4);

  // Space in packet available for future use.
  //loraSendNumericValue(RADIO_MSG_, speeduinoSensors.);
  //loraSendNumericValue(RADIO_MSG_, speeduinoSensors.);

  LoRa.endPacket(true /* async */);

  return 1;
}

void setup()
{
  memset(&statusMessages, 0, sizeof(StatusMessages));
  memset(&lastRenderedStatusMessages, 0, sizeof(StatusMessages));

  memset(&secondaryInfo, 0, sizeof(SecondaryInfo));
  memset(&lastRenderedSecondaryInfo, 0, sizeof(SecondaryInfo));

  DebugSerial.begin(115200);

  tachDisplayInit();
  clearTachLights();

  okColors.bar = ILI9341_CYAN;
  okColors.background = BACKGROUND_COLOR;
  okColors.text = ILI9341_WHITE;

  errorColors.bar = ILI9341_RED;
  errorColors.background = BACKGROUND_COLOR;
  errorColors.text = ILI9341_ORANGE;

  tft.begin();
  tft.setRotation(1);
  requestFrame = false;


  #ifdef BOOTSPLASH
  renderBootImage();
  #else
  renderNoConnection();
  #endif

  tft.setTextSize(2);
  fontHeightSize2 = tft.fontHeight();
  charWidthSize2 = tft.textWidth(" ");

  DebugSerial.println("initializing radio");
  radioSPI.begin();
  LoRa.setPins(PB12 /* CS */, PB5 /* Reset */, PC14 /* IRQ */);

  // initialize radio at 915 MHz
  radioAvailable = LoRa.begin(915E6, false /* useLNA */, &radioSPI);
  radioBusy = false;

  if (radioAvailable) {
    DebugSerial.println("radio init ok");

    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.setSpreadingFactor(7);
    LoRa.setPreambleLength(8);
    LoRa.enableCrc();
    LoRa.setTxPower(20);
  } else {
    DebugSerial.println("radio init failed");
  }

  SpeeduinoSerial.setRx(PA3);
  SpeeduinoSerial.setTx(PA2);
  SpeeduinoSerial.setTimeout(500);
  SpeeduinoSerial.begin(115200); // speeduino runs at 115200

  tachBootAnimation();

  missionStartTimeMillis = millis();
  lastTelemetryPacketSentAtMillis = 0;
  fpsCounterStartTime = millis();
  frameCounter = 0;
}

void loop(void)
{
  updateAllSensors();
  updateTach(speeduinoSensors.RPM, 2000 /* firstLightRPM */, LIMIT_RPM_UPPER, false /* idiotLight TODO */);
  if ((millis() - lastTelemetryPacketSentAtMillis) >= TELEMETRY_UPDATE_PERIOD_MS) {
    bool sent = loraSendTelemetryPacket();
    if (sent) {
      lastTelemetryPacketSentAtMillis = millis();
    }
  }
  updateSecondaryInfo();
  updateStatusMessages();

  if (screenState != lastScreenState || requestFrame)
  {
    if (screenState != lastScreenState)
    {
      clearScreen();
    }

    switch (screenState)
    {
    case SCREEN_STATE_NO_DATA:
      renderNoData();
      break;
    case SCREEN_STATE_NO_CONNECTION:
      renderNoConnection();
      break;
    case SCREEN_STATE_NORMAL:
      render(screenState != lastScreenState);
      break;
    default:
      render(screenState != lastScreenState);
      break;
    }
  }

  lastScreenState = screenState;
  requestFrame = false;
}
