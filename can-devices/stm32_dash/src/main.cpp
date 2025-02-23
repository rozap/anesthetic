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
// #define DEBUG_SPEEDUINO_COMMS

// Extra debugging for LoRa comms.
// #define DEBUG_LORA_COMMS

// Extra debugging for oil temp analog reading (useful for calibration).
// #define DEBUG_OIL_TEMPERATURE_ANALOG_READING

// Extra debugging for fuel level analog reading (useful for calibration).
// #define DEBUG_FUEL_LEVEL_ANALOG_READING

// Display the bootsplash (disable if debugging to shorten upload times).
// #define BOOTSPLASH false

// Use data in mock_pkt.h instead of actually reading from the serial port.
// #define USE_MOCK_DATA

#define LIMIT_COOLANT_UPPER 215
#define LIMIT_OIL_LOWER 10
#define LIMIT_FUEL_LOWER 10
#define LIMIT_FUEL_PRESSURE_LOWER 15

#define LIMIT_RPM_UPPER 5800
#define LIMIT_VOLTAGE_LOWER 12.0

// Telemetry is sent at most this often. It will usually be less often, as the radio will
// often be busy/unable to send at the exact moment. This is essentially the radio polling
// rate.
#define TELEMETRY_MIN_SEND_PERIOD_MS 200

// Averaging window size for analog readings (oil temp and fuel level).
#define WINDOW_SIZE 16

// Height of gauges.
#define BAR_HEIGHT 8

// End config

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <CircularBuffer.hpp>
#include <SoftwareSerial.h>
#include <mcp2515.h>

#include "tach.h"
#include "state.h"
#include "can.h"

/* Radio */

// Some vestigial RadioHead stuff we can refactor out later.

#ifdef BOOTSPLASH
#include "boot_image.h"
#endif

#ifdef USE_MOCK_DATA
#include "mock_pkt.h"
#endif

CurrentEngineState currentEngineState;

// Serial1: RX: A10, TX: A9. Debug and programming port.
HardwareSerial DebugSerial = Serial1;

long missionStartTimeMillis;
bool everHadEcuData;

#define WIDTH 320
#define HEIGHT 240

#define ADC_FULL_SCALE_V 3.3

/*
 * VIN ---- /\/\/\----+-----/\/\/\---- GND
 * 5.0V     REF_OHM   |     SENSOR
 *                   ADC
 * ADC reads from 0-3.3V!
 */
#define FUEL_VIN 5.0
#define FUEL_ANALOG_PIN PB0
// Chosen so output falls within ADC range. Actual resistor hand-measured
// with EEVBlog 121GW multimeter.
#define FUEL_REF_OHM 145

/*
 * VIN ---- /\/\/\----+-----/\/\/\---- GND
 * 5.0V     REF_OHM   |     SENSOR
 *                   ADC
 * ADC reads from 0-3.3V!
 * NTC 10k. Temperature goes up, resistance goes down.
 */
#define OIL_TEMP_VIN 5.0
#define OIL_TEMP_ANALOG_PIN PB1
#define OIL_TEMP_REF_OHM 47640 // Measured.

// 16 bit TFT, 5 bits red, 6 green, 5 blue
#define BACKGROUND_COLOR ILI9341_BLACK
#define BAR_COLOR ILI9341_CYAN
bool requestFrame = false;

#define SCREEN_STATE_NO_DATA 1
#define SCREEN_STATE_NO_CONNECTION 2
#define SCREEN_STATE_NORMAL 0
uint8_t screenState = SCREEN_STATE_NO_CONNECTION;
uint8_t lastScreenState = SCREEN_STATE_NO_CONNECTION;

// Render structs used to only re-render what we need (fps 4 -> ~30).

struct StatusMessages
{
  bool fanOff;
  bool fanOn;
  bool engHot;
  bool lowGas;
  bool lowOilPressure;
  bool lowFuelPressure;
  bool overRev;
  bool allOk;
  bool engOff;
  bool running;
  bool cranking;
  bool warmup;
  bool ase;
  bool lowVolt;

  bool operator==(const StatusMessages &) const = default; // Auto == operator.
} statusMessages, lastRenderedStatusMessages;

struct CoreInfo
{
  int RPM;
  int oilPressure;
  int coolantTemp;
  double volts;

  bool operator==(const CoreInfo &) const = default; // Auto == operator.
} coreInfo, lastRenderedCoreInfo;

struct SecondaryInfo
{
  double airFuel;
  int oilTemp;
  int fuelPressure;
  int iat;
  double volts;
  int missionElapsedSeconds;

  bool operator==(const SecondaryInfo &) const = default; // Auto == operator.
} secondaryInfo, lastRenderedSecondaryInfo;

struct Colors
{
  int bar;
  int background;
  int text;
} okColors, errorColors;

bool isNthBitSet(unsigned char c, int n)
{
  // static unsigned char mask[] = {128, 64, 32, 16, 8, 4, 2, 1};
  static unsigned char mask[] = {1, 2, 4, 8, 16, 32, 64, 128};

  return ((c & mask[n]) != 0);
}

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

TFT_eSPI tft = TFT_eSPI();

// SPI port 2
//                COPI   CIPO   SCLK  PSEL
// SPIClass radioSPI(PB15, PB14, PB13); //, PB12);

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
  for (int i = 0; i < cb.size(); i++)
  {
    total += cb[i];
  }
  return total / cb.size();
}

void clearLine()
{
  tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.width(), tft.getCursorY() + tft.textsize, BACKGROUND_COLOR);
}

void drawPlainGauge(int value, int min, int max, int y, int height, int color)
{
  int width = map(value, min, max, 0, tft.width());

  tft.fillRect(width, y, tft.width() - width, height, BACKGROUND_COLOR);

  tft.fillRect(
      0, y,
      width, height,
      color);
}

// NOTE: numFmt should return a fixed-length string. This allows us to not worry about clearing
// old values from the screen - this cuts frame times by like a third.
void drawLabeledGauge(
    bool firstRender,
    const char *label,
    const char *numberFormat,
    int value,
    int min,
    int max,
    int warnLow,
    int warnHigh,
    Colors colorsGood,
    Colors colorsBad)
{
  int numberXPos = charWidthSize2 * strlen(label);

  bool bad = value < warnLow || value > warnHigh;
  tft.setTextSize(2);

  if (firstRender)
  {
    tft.setTextColor(colorsGood.text, colorsGood.background);
    tft.print(label);
  }

  if (bad)
  {
    tft.setTextColor(colorsBad.text, colorsBad.background);
  }
  else
  {
    tft.setTextColor(colorsGood.text, colorsGood.background);
  }

  tft.setCursor(numberXPos, tft.getCursorY());
  tft.printf(
      numberFormat,
      value);
  tft.println();

  int y = tft.getCursorY();
  int zoneMarkerHeight = 1;
  drawPlainGauge(value, min, max, y, BAR_HEIGHT - zoneMarkerHeight - 1, bad ? colorsBad.bar : colorsGood.bar);
  if (firstRender)
  {
    int pxPosWarnLow = map(warnLow, min, max, 0, tft.width());
    int pxPosWarnHigh = map(warnHigh, min, max, 0, tft.width());
    tft.fillRect(
        0, y + BAR_HEIGHT - zoneMarkerHeight,
        tft.width(), zoneMarkerHeight,
        colorsBad.bar);
    tft.fillRect(
        pxPosWarnLow, y + BAR_HEIGHT - zoneMarkerHeight,
        pxPosWarnHigh - pxPosWarnLow, zoneMarkerHeight,
        colorsGood.bar);
  }

  tft.println();
}

void clearScreen()
{
  tft.fillScreen(ILI9341_BLACK);
}

void moveToHalfWidth()
{
  tft.setCursor(WIDTH / 2 + 8, tft.getCursorY());
  clearLine();
}

void writeSingleStatusMessage(bool enabled, const char *msg)
{
  if (enabled)
  {
    moveToHalfWidth();
    tft.println(msg);
  }
}

void renderStatusMessages(int bottomPanelY)
{
  if (statusMessages == lastRenderedStatusMessages)
  {
    return;
  }

  lastRenderedStatusMessages = statusMessages;

  tft.setCursor(WIDTH / 2 + 8, bottomPanelY);
  tft.fillRect(WIDTH / 2 + 1, bottomPanelY - 4, WIDTH, HEIGHT, BACKGROUND_COLOR);

  tft.setTextColor(okColors.text);
  writeSingleStatusMessage(statusMessages.fanOn, "Fan On");
  writeSingleStatusMessage(statusMessages.fanOff, "Fan Off");

  tft.setTextColor(errorColors.text);
  writeSingleStatusMessage(statusMessages.engHot, "OVER TEMP!");
  writeSingleStatusMessage(statusMessages.lowGas, "FUEL QTY!");
  writeSingleStatusMessage(statusMessages.lowOilPressure, "OIL PRES!");
  writeSingleStatusMessage(statusMessages.lowFuelPressure, "OIL PRES!");

  writeSingleStatusMessage(statusMessages.overRev, "ENG RPM!");
  writeSingleStatusMessage(statusMessages.lowVolt, "LOW VOLT!");
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

  if (firstRender)
  {
    tft.println("A/F");
    tft.println("FL PRS");
    tft.println("OIL T");
    tft.println("IAT");
    tft.println("VOLTS");
    tft.println("STINT");
  }

  if (lastRenderedSecondaryInfo.airFuel != secondaryInfo.airFuel)
  {
    tft.setCursor(numberXPos, numberYPos);
    tft.printf(
        "%5.1f",
        secondaryInfo.airFuel);
  }

  if (lastRenderedSecondaryInfo.fuelPressure != secondaryInfo.fuelPressure)
  {
    if (statusMessages.lowFuelPressure)
    {
      tft.setTextColor(errorColors.text, BACKGROUND_COLOR);
    }
    tft.setCursor(numberXPos, numberYPos + fontHeightSize2 * 2);
    tft.printf("%2d", secondaryInfo.fuelPressure);

    if (statusMessages.lowFuelPressure)
    {
      tft.setTextColor(okColors.text, BACKGROUND_COLOR);
    }
  }

  if (lastRenderedSecondaryInfo.oilTemp != secondaryInfo.oilTemp)
  {
    tft.setCursor(numberXPos + charWidthSize2 * 2, numberYPos + fontHeightSize2);
    tft.printf("%3d", secondaryInfo.oilTemp);
  }

  if (lastRenderedSecondaryInfo.iat != secondaryInfo.iat)
  {
    tft.setCursor(numberXPos + charWidthSize2, numberYPos + fontHeightSize2 * 3);
    tft.printf("%4d", secondaryInfo.iat);
  }

  if (lastRenderedSecondaryInfo.volts != secondaryInfo.volts)
  {
    if (statusMessages.lowVolt)
    {
      tft.setTextColor(errorColors.text, BACKGROUND_COLOR);
    }
    tft.setCursor(numberXPos, numberYPos + fontHeightSize2 * 4);
    tft.printf("%5.1f", secondaryInfo.volts);
    if (statusMessages.lowVolt)
    {
      tft.setTextColor(okColors.text, BACKGROUND_COLOR);
    }
  }

  if (lastRenderedSecondaryInfo.missionElapsedSeconds != secondaryInfo.missionElapsedSeconds)
  {
    tft.setCursor(numberXPos, numberYPos + fontHeightSize2 * 5);
    tft.printf(
        "%02u:%02u",
        secondaryInfo.missionElapsedSeconds / 60,
        secondaryInfo.missionElapsedSeconds % 60);
  }

  lastRenderedSecondaryInfo = secondaryInfo;
}

void renderNoConnection()
{
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setCursor(0, 0);
  tft.println("No Connection");
  DebugSerial.println("no connection");
}

void renderNoData()
{
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(4);
  tft.setCursor(0, 0);
  clearLine();
  tft.println("No Data");

  tft.setTextSize(2);
  clearLine();
  tft.print("Timeouts ");
  tft.print(currentEngineState.missedMessageCount);
  tft.println("  ");

  tft.setTextSize(2);
  clearLine();
  tft.print("CAN Status ");

  switch (currentEngineState.canState)
  {
  case MCP2515::ERROR_OK:
    tft.print("Err Ok??");
    break;

  case MCP2515::ERROR_FAIL:
    tft.print("Err Fail");
    break;
  case MCP2515::ERROR_ALLTXBUSY:
    tft.print("Err all tx busy");
    break;
  case MCP2515::ERROR_FAILINIT:
    tft.print("Err fail init");
    break;
  case MCP2515::ERROR_FAILTX:
    tft.print("Err fail tx");
    break;
  case MCP2515::ERROR_NOMSG:
    tft.print("Err No Message");
    break;
  }
  tft.println("  ");

  clearLine();
  tft.print("Fuel ");
  tft.print(localSensors.fuelPct);
  tft.println("%   ");

  DebugSerial.println("no data");
}

void render(bool firstRender)
{
  tft.setTextSize(3);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_CYAN);

  int fuel = localSensors.fuelPct;
  drawLabeledGauge(firstRender, "FUEL    ", "%3d", fuel, 0, 100, LIMIT_FUEL_LOWER, 100, okColors, errorColors);
  drawLabeledGauge(firstRender, "RPM    ", "%4d", currentEngineState.RPM, 500, 7000, 0, LIMIT_RPM_UPPER, okColors, errorColors);

  int coolantF = (int)(((float)currentEngineState.coolantTemp) * 1.8 + 32);
  drawLabeledGauge(firstRender, "COOLANT ", "%3d", coolantF, 50, 250, 0, LIMIT_COOLANT_UPPER, okColors, errorColors);
  drawLabeledGauge(firstRender, "OIL     ", "%3d", currentEngineState.oilPressure, 0, 60, LIMIT_OIL_LOWER, 999, okColors, errorColors);

  int bottomPanelY = tft.getCursorY();
  if (firstRender)
  {
    tft.drawLine(WIDTH / 2, bottomPanelY, WIDTH / 2, HEIGHT, ILI9341_WHITE);
    tft.drawLine(0, bottomPanelY, WIDTH, bottomPanelY, ILI9341_WHITE);
  }

  tft.setTextSize(2);

  bottomPanelY = bottomPanelY + 6;
  tft.setCursor(0, bottomPanelY);
  renderSecondaries(firstRender, bottomPanelY);
  renderStatusMessages(bottomPanelY);

  requestFrame = false;

  if (frameCounter >= 50)
  {
    double secondsForFrames = (millis() - fpsCounterStartTime) / 1000.0;
    fpsCounterStartTime = millis();
    DebugSerial.print("render FPS: ");
    DebugSerial.println(frameCounter / secondsForFrames);
    frameCounter = 0;
  }
  else
  {
    frameCounter++;
  }
}

#ifdef BOOTSPLASH
void renderBootImage()
{
  uint32_t i = 0;
  for (uint16_t y = 0; y < boot_image_height; y++)
  {
    for (uint16_t x = 0; x < boot_image_width; x++)
    {
      tft.drawPixel(x, y, boot_image_data[i]);
      i++;
    }
  }
}
#endif

void updateSecondaryInfoForRender()
{
  secondaryInfo.airFuel = currentEngineState.lambda * 14.68;
  secondaryInfo.fuelPressure = currentEngineState.fuelPressure;
  secondaryInfo.oilTemp = currentEngineState.oilTemp;
  secondaryInfo.iat = currentEngineState.iat;
  secondaryInfo.volts = currentEngineState.volts;
  secondaryInfo.missionElapsedSeconds = localSensors.missionElapsedSeconds;
}

void updateStatusMessagesForRender()
{
  double coolantF = ((double)currentEngineState.coolantTemp) * 1.8 + 32;
  double voltage = currentEngineState.volts;
  statusMessages.fanOn = currentEngineState.fanOn; // TOO
  statusMessages.fanOff = true;                    // TODO
  statusMessages.engHot = false;                   // TODO
  statusMessages.lowGas = localSensors.fuelPct < LIMIT_FUEL_LOWER;
  statusMessages.lowOilPressure = currentEngineState.oilPressure < LIMIT_OIL_LOWER;
  statusMessages.lowFuelPressure = currentEngineState.fuelPressure < LIMIT_FUEL_PRESSURE_LOWER;

  statusMessages.overRev = currentEngineState.RPM > LIMIT_RPM_UPPER;
  statusMessages.running = false;  // TODO
  statusMessages.cranking = false; // TODO
  statusMessages.warmup = false;   // TODO
  statusMessages.ase = false;      // TODO
  statusMessages.engOff = !statusMessages.running && !statusMessages.cranking;
  statusMessages.lowVolt = voltage < LIMIT_VOLTAGE_LOWER;

  statusMessages.allOk = !(
      statusMessages.engHot ||
      // statusMessages.lowGas ||
      statusMessages.lowOilPressure ||
      statusMessages.overRev ||
      statusMessages.lowVolt);
}

#define RESPONSE_LEN 128
uint8_t speeduinoResponse[RESPONSE_LEN];

// Call this once per update interval.
// Consumers should look at localSensors.fuelPct.
void updateFuel()
{
  int ain = analogRead(FUEL_ANALOG_PIN);
  double adcVolts = ain * ADC_FULL_SCALE_V / 4096.0;
  double ohms = FUEL_REF_OHM * (adcVolts / (FUEL_VIN - adcVolts));
#ifdef DEBUG_FUEL_LEVEL_ANALOG_READING
  DebugSerial.print("FUEL LEVEL DEBUG: raw reading: ");
  DebugSerial.println(ain);
  DebugSerial.print("FUEL LEVEL DEBUG: voltage seen: ");
  DebugSerial.println(adcVolts);
  DebugSerial.print("FUEL LEVEL DEBUG: calculated ohms: ");
  DebugSerial.println(ohms);
#endif

  fuelWindow.push(ohms);
  double avgOhms = avg(fuelWindow);
  /*
    Logarithmic fit of experimental values:
    Ohms      Pct
    36      100
    85.3      50
    239.2     0
  */

  double pct = 287.120 + (-52.665 * log(avgOhms));

#ifdef DEBUG_FUEL_LEVEL_ANALOG_READING
  DebugSerial.print("FUEL LEVEL DEBUG: calculated avg ohms: ");
  DebugSerial.println(avgOhms);
  DebugSerial.print("FUEL LEVEL DEBUG: calculated avg percentage: ");
  DebugSerial.println(pct);
#endif

  if (pct < 0)
  {
    pct = 0;
  }

  if (pct > 100)
  {
    pct = 100;
  }

#ifdef USE_MOCK_DATA
  pct = sin(millis() / 1000.0) * 50.0 + 50.0;
#endif

  localSensors.fuelPct = (int)pct;
}

// TODO: use the ECU adc to do this and emit over CAN
// // Call this once per update interval.
// // Consumers should look at localSensors.oilTemp.
// void updateOilT()
// {
//   int ain = analogRead(OIL_TEMP_ANALOG_PIN);
//   double adcVolts = ain * ADC_FULL_SCALE_V / 4096.0;
//   double ohms = OIL_TEMP_REF_OHM * (adcVolts / (OIL_TEMP_VIN - adcVolts));
//   // Manual amazon calibration LOL
//   double tempF = -43.609*log(ohms) + 551.864;

//   #ifdef DEBUG_OIL_TEMPERATURE_ANALOG_READING
//   DebugSerial.print("OIL TEMPERATURE DEBUG: raw reading: ");
//   DebugSerial.println(ain);
//   DebugSerial.print("OIL TEMPERATURE DEBUG: voltage seen: ");
//   DebugSerial.println(adcVolts);
//   DebugSerial.print("OIL TEMPERATURE DEBUG: calculated ohms: ");
//   DebugSerial.println(ohms);
//   DebugSerial.print("OIL TEMPERATURE DEBUG: calculated tempF: ");
//   DebugSerial.println(tempF);
//   #endif

//   oilTempWindow.push(tempF);
//   double avgTempF = avg(oilTempWindow);

//   #ifdef USE_MOCK_DATA
//   avgTempF = sin(millis() / 700.0) * 100.0 + 150.0;
//   #endif

//   #ifdef DEBUG_OIL_TEMPERATURE_ANALOG_READING
//   DebugSerial.print("OIL TEMPERATURE DEBUG: calculated avg tempF: ");
//   DebugSerial.println(avgTempF);
//   #endif

//   localSensors.oilTemp = avgTempF;
// }

// Called once per frame.
void updateLocalSensors()
{
  // requestSpeeduinoUpdate();
  updateFuel();
  // updateOilT();
  localSensors.missionElapsedSeconds = (millis() - missionStartTimeMillis) / 1000;
}

void setup()
{
  memset(&statusMessages, 0, sizeof(StatusMessages));
  memset(&lastRenderedStatusMessages, 0, sizeof(StatusMessages));

  memset(&secondaryInfo, 0, sizeof(SecondaryInfo));
  memset(&lastRenderedSecondaryInfo, 0, sizeof(SecondaryInfo));

  memset(&coreInfo, 0, sizeof(CoreInfo));
  memset(&lastRenderedCoreInfo, 0, sizeof(CoreInfo));

  everHadEcuData = false;

  DebugSerial.begin(115200);

  analogReadResolution(12);

  // Power supply can take a bit to start. Mostly a concern for the LED driver, which runs
  // directly on 5V (everything else is on a derived 3V3 supply). We've had issues with the
  // driver coming up consistently, so we just wait a bit.
  delay(250);
  tachDisplayInit();
  // canInit();
  clearTachLights();

  okColors.bar = ILI9341_CYAN;
  okColors.background = BACKGROUND_COLOR;
  okColors.text = ILI9341_WHITE;

  errorColors.bar = ILI9341_RED;
  errorColors.background = BACKGROUND_COLOR;
  errorColors.text = ILI9341_ORANGE;

  tft.begin();
  tft.setRotation(1);
  requestFrame = true;

#ifdef BOOTSPLASH
  renderBootImage();
#else
  renderNoConnection();
#endif

  tft.setTextSize(2);
  fontHeightSize2 = tft.fontHeight();
  charWidthSize2 = tft.textWidth(" ");

  tachBootAnimation();

  missionStartTimeMillis = millis();
  fpsCounterStartTime = millis();
  frameCounter = 0;
}

void loop(void)
{
  DebugSerial.println("Foo!");
  delay(500);
  return;
  updateState(currentEngineState);
  if (currentEngineState.canState != MCP2515::ERROR_OK)
  {
    // in the state update, if X time goes by, it will increment missedMessageCount
    // so if that happens N times, we'll assume a lost connection and render no data
    if (currentEngineState.missedMessageCount > 3)
    {
      screenState = SCREEN_STATE_NO_DATA;
    }
  }

  updateLocalSensors();

  if (screenState == SCREEN_STATE_NORMAL)
  {
    bool idiotLight = !statusMessages.allOk;
    updateTach(currentEngineState.RPM, 2000 /* firstLightRPM */, LIMIT_RPM_UPPER, idiotLight, statusMessages.running);
  }

  updateSecondaryInfoForRender();
  updateStatusMessagesForRender();

  if (screenState != lastScreenState || requestFrame)
  {
    if (screenState != lastScreenState)
    {
      clearScreen();
    }

    switch (screenState)
    {
    case SCREEN_STATE_NO_DATA:
      clearTachLights();
      renderNoData();
      break;
    case SCREEN_STATE_NO_CONNECTION:
      clearTachLights();
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
