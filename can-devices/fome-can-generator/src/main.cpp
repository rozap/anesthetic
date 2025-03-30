#include <Arduino.h>
#include <mcp2515.h>

HardwareSerial DebugSerial = Serial1;

// put function declarations here:
// Helper function to get random float within range
float randomFloat(float min, float max)
{
  return min + (random(1000000L) / 1000000.0) * (max - min);
}

// Helper function to get random int within range
long randomInt(long min, long max)
{
  return random(min, max + 1);
}

#define SWEEP_PERIOD_MS 5000 // 5 second period for all values

// Sweeping function that returns a value that oscillates between min and max
float sweep(float min, float max)
{
  // Get current position in the sweep (0.0 to 1.0)
  float position = (float)(millis() % SWEEP_PERIOD_MS) / SWEEP_PERIOD_MS;

  // Use sine wave to sweep smoothly between min and max
  float sweep = sin(position * 2 * PI); // -1 to 1
  sweep = (sweep + 1) / 2;              // Convert to 0 to 1

  return min + (max - min) * sweep;
}

// Integer version of sweep function
long sweepInt(long min, long max)
{
  return (long)sweep(min, max);
}

// Boolean sweep
bool sweepBool()
{
  return (millis() % SWEEP_PERIOD_MS) > (SWEEP_PERIOD_MS / 2);
}

// Generate test data for BASE0 (512)
void generateBase0Frame(can_frame &frame)
{
  frame.can_id = 512;
  frame.can_dlc = 8;

  // Warning counter (0-65535)
  uint16_t warningCounter = sweepInt(0, 65535);
  frame.data[0] = warningCounter & 0xFF;
  frame.data[1] = (warningCounter >> 8) & 0xFF;

  // Last error (0-65535)
  uint16_t lastError = sweepInt(0, 65535);
  frame.data[2] = lastError & 0xFF;
  frame.data[3] = (lastError >> 8) & 0xFF;

  // Status bits - all toggle at same rate
  uint16_t statusBits = 0;
  bool bitState = sweepBool();
  statusBits |= bitState ? 0x01 : 0; // RevLimAct
  statusBits |= bitState ? 0x02 : 0; // MainRelayAct
  statusBits |= bitState ? 0x04 : 0; // FuelPumpAct
  statusBits |= bitState ? 0x08 : 0; // CELAct
  statusBits |= bitState ? 0x10 : 0; // EGOHeatAct
  statusBits |= bitState ? 0x20 : 0; // LambdaProtectAct
  statusBits |= bitState ? 0x40 : 0; // Fan
  statusBits |= bitState ? 0x80 : 0; // Fan2

  frame.data[4] = statusBits & 0xFF;
  frame.data[5] = (statusBits >> 8) & 0xFF;

  // Current gear (0-8)
  frame.data[5] |= sweepInt(0, 8) & 0xFF;

  // Distance traveled (0-6553.5 km)
  uint16_t distance = sweep(0, 6553.5) * 10;
  frame.data[6] = distance & 0xFF;
  frame.data[7] = (distance >> 8) & 0xFF;
}

// Generate test data for BASE1 (513)
void generateBase1Frame(can_frame &frame)
{
  frame.can_id = 513;
  frame.can_dlc = 8;

  // RPM (0-12000)
  uint16_t rpm = sweepInt(0, 8000);
  frame.data[0] = rpm & 0xFF;
  frame.data[1] = (rpm >> 8) & 0xFF;

  // Ignition timing (-50 to 50 degrees)
  int16_t timing = sweep(-50, 50) / 0.02f;
  frame.data[2] = timing & 0xFF;
  frame.data[3] = (timing >> 8) & 0xFF;

  // Injection duty (0-100%)
  frame.data[4] = sweep(0, 100) / 0.5f;

  // Ignition duty (0-100%)
  frame.data[5] = sweep(0, 100) / 0.5f;

  // Vehicle speed (0-255 kph)
  frame.data[6] = sweepInt(0, 255);

  // Flex fuel percentage (0-100%)
  frame.data[7] = sweepInt(0, 100);
}

// Generate test data for BASE3 (515)
void generateBase3Frame(can_frame &frame)
{
  frame.can_id = 515;
  frame.can_dlc = 8;

  // MAP (0-300 kPa)
  uint16_t map = sweep(0, 300) / 0.03333333f;
  frame.data[0] = map & 0xFF;
  frame.data[1] = (map >> 8) & 0xFF;

  // Coolant temp (-40 to 200 C)
  frame.data[2] = sweepInt(-40, 200) + 40;

  // Intake temp (-40 to 200 C)
  frame.data[3] = sweepInt(-40, 200) + 40;

  // AUX1 temp (-40 to 200 C)
  frame.data[4] = sweepInt(-40, 200) + 40;

  // AUX2 temp (-40 to 200 C)
  frame.data[5] = sweepInt(-40, 200) + 40;

  // MCU temp (-40 to 100 C)
  frame.data[6] = sweepInt(-40, 100) + 40;

  // Fuel level (0-100%)
  frame.data[7] = sweep(0, 100) / 0.5f;
}

// Generate test data for BASE4 (516)
void generateBase4Frame(can_frame &frame)
{
  frame.can_id = 516;
  frame.can_dlc = 8;

  // First 2 bytes unused in original decoder
  frame.data[0] = 0;
  frame.data[1] = 0;

  // Oil pressure (0-1000 kPa)
  uint16_t oilPress = sweep(0, 1000) / 0.03333333f;
  frame.data[2] = oilPress & 0xFF;
  frame.data[3] = (oilPress >> 8) & 0xFF;

  // Oil temperature (-40 to 215 C)
  frame.data[4] = sweepInt(-40, 215) + 40;

  // Fuel temperature (-40 to 215 C)
  frame.data[5] = sweepInt(-40, 215) + 40;

  // Battery voltage (0-25V)
  uint16_t battVolt = sweep(0, 25) / 0.001f;
  frame.data[6] = battVolt & 0xFF;
  frame.data[7] = (battVolt >> 8) & 0xFF;
}

// Generate test data for BASE5 (517)
void generateBase5Frame(can_frame &frame)
{
  frame.can_id = 517;
  frame.can_dlc = 8;

  // Cylinder air mass (0-1000 mg)
  uint16_t cylAM = sweepInt(0, 1000);
  frame.data[0] = cylAM & 0xFF;
  frame.data[1] = (cylAM >> 8) & 0xFF;

  // Estimated MAF (0-500 kg/h)
  uint16_t estMAF = sweep(0, 500) / 0.01f;
  frame.data[2] = estMAF & 0xFF;
  frame.data[3] = (estMAF >> 8) & 0xFF;

  // Injection pulse width (0-20 ms)
  uint16_t injPW = sweep(0, 20) / 0.003333333f;
  frame.data[4] = injPW & 0xFF;
  frame.data[5] = (injPW >> 8) & 0xFF;

  // Knock count (0-1000)
  uint16_t knockCt = sweepInt(0, 1000);
  frame.data[6] = knockCt & 0xFF;
  frame.data[7] = (knockCt >> 8) & 0xFF;
}

// Generate test data for BASE6 (518)
void generateBase6Frame(can_frame &frame)
{
  frame.can_id = 518;
  frame.can_dlc = 8;

  // Fuel used (0-65535 g)
  uint16_t fuelUsed = sweepInt(0, 65535);
  frame.data[0] = fuelUsed & 0xFF;
  frame.data[1] = (fuelUsed >> 8) & 0xFF;

  // Fuel flow (0-327 g/s)
  uint16_t fuelFlow = sweep(0, 327) / 0.005f;
  frame.data[2] = fuelFlow & 0xFF;
  frame.data[3] = (fuelFlow >> 8) & 0xFF;

  // Fuel trim 1 (-50 to 50%)
  int16_t fuelTrim1 = sweep(-50, 50) / 0.01f;
  frame.data[4] = fuelTrim1 & 0xFF;
  frame.data[5] = (fuelTrim1 >> 8) & 0xFF;

  // Fuel trim 2 (-50 to 50%)
  int16_t fuelTrim2 = sweep(-50, 50) / 0.01f;
  frame.data[6] = fuelTrim2 & 0xFF;
  frame.data[7] = (fuelTrim2 >> 8) & 0xFF;
}

// Generate test data for BASE7 (519)
void generateBase7Frame(can_frame &frame)
{
  frame.can_id = 519;
  frame.can_dlc = 8;

  // Lambda 1 (0.7-1.3 lambda)
  uint16_t lambda1 = sweep(0.7, 1.3) / 0.0001f;
  frame.data[0] = lambda1 & 0xFF;
  frame.data[1] = (lambda1 >> 8) & 0xFF;

  // Lambda 2 (0.7-1.3 lambda)
  uint16_t lambda2 = sweep(0.7, 1.3) / 0.0001f;
  frame.data[2] = lambda2 & 0xFF;
  frame.data[3] = (lambda2 >> 8) & 0xFF;

  // Fuel pressure low (0-1000 kPa)
  uint16_t fpLow = sweep(0, 1000) / 0.03333333f;
  frame.data[4] = fpLow & 0xFF;
  frame.data[5] = (fpLow >> 8) & 0xFF;

  // Fuel pressure high (0-100 bar)
  uint16_t fpHigh = sweep(0, 100) / 0.1f;
  frame.data[6] = fpHigh & 0xFF;
  frame.data[7] = (fpHigh >> 8) & 0xFF;
}
MCP2515 mcp2515(PA4);
#define CAN_FRAME_BUFFER_LEN 10
volatile uint8_t canFrame = 0;
struct can_frame frames[CAN_FRAME_BUFFER_LEN];

void sendFrame(can_frame &frame)
{
  MCP2515::ERROR res = mcp2515.sendMessage(&frame);
  DebugSerial.printf("CAN TX res %d ID %d DLC %d: ", res, frame.can_id, frame.can_dlc);
  for (size_t i = 0; i < frame.can_dlc; i++)
  {
    if (frame.data[i] < 0x10)
      DebugSerial.print('0'); // pad single digit hex numbers with leading 0
    DebugSerial.print(frame.data[i], HEX);
    DebugSerial.print(' ');
  }
  DebugSerial.println("");
}

void setup()
{
  DebugSerial.begin(115200);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
}

void loop()
{
  for (int i = 0; i < 8; i++)
  {
    switch (i)
    {
    case 0:
      generateBase0Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 1:
      generateBase1Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 2:
      // generateBase2Frame(frames[i]);
      // sendFrame(frames[i]);
      break;
    case 3:
      generateBase3Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 4:
      generateBase4Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 5:
      generateBase5Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 6:
      generateBase6Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    case 7:
      generateBase7Frame(frames[i]);
      sendFrame(frames[i]);
      break;
    }
  }
  delay(50);
}
