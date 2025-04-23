#include <SPI.h>
#include <mcp2515.h>
#include "state.h"

#define CAN_MIN_ADDRESS 508
#define CAN_MAX_ADDRESS 519
#define CAN_FRAME_BUFFER_LEN (CAN_MAX_ADDRESS - CAN_MIN_ADDRESS) + 1
struct can_frame frames[CAN_FRAME_BUFFER_LEN];
long lastFrames[CAN_FRAME_BUFFER_LEN];
struct can_frame tempFrame;
#define INT_PIN PB5
MCP2515 mcp2515(PB12);

// don't accept more than 1 frame of each type within this period
// of milliseconds
#define FRAME_THROTTLE_MS 10
volatile uint8_t frameCount = 0;
void copyTempFrame()
{
  if (tempFrame.can_id >= CAN_MIN_ADDRESS && tempFrame.can_id <= CAN_MAX_ADDRESS)
  {
    uint8_t index = tempFrame.can_id - CAN_MIN_ADDRESS;
    long now = millis();
    if ((now - lastFrames[index]) > FRAME_THROTTLE_MS)
    {
      memcpy(&frames[index], &tempFrame, sizeof(struct can_frame));
      lastFrames[index] = now;
      frameCount = frameCount + 1;
    }
  }
}

void irqHandler()
{
  noInterrupts();
  uint8_t hasIrq = mcp2515.getInterrupts();
  if (hasIrq & MCP2515::CANINTF_RX0IF)
  {
    MCP2515::ERROR rx0res = mcp2515.readMessage(MCP2515::RXB0, &tempFrame);
    if (rx0res == MCP2515::ERROR_OK)
    {
      copyTempFrame();
    }
  }

  if (hasIrq & MCP2515::CANINTF_RX1IF)
  {
    MCP2515::ERROR rx1res = mcp2515.readMessage(MCP2515::RXB1, &tempFrame);
    if (rx1res == MCP2515::ERROR_OK)
    {
      copyTempFrame();
    }
  }
  interrupts();
}

void canInit()
{
  SPI.setMOSI(PB15);
  SPI.setMISO(PB14);
  SPI.setSCLK(PB13);
  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  pinMode(PB5, INPUT_PULLUP);
  attachInterrupt(PB5, irqHandler, FALLING);

  for (int i = 0; i < CAN_FRAME_BUFFER_LEN; i++)
  {
    memset(&frames[i], 0, sizeof(can_frame));
    lastFrames[i] = 0;
  }
}

void printFrame(can_frame frame)
{
  DebugSerial.printf("CAN RX ID %d DLC %d: ", frame.can_id, frame.can_dlc);
  for (size_t i = 0; i < frame.can_dlc; i++)
  {
    if (frame.data[i] < 0x10)
      DebugSerial.print('0'); // pad single digit hex numbers with leading 0
    DebugSerial.print(frame.data[i], HEX);
    DebugSerial.print(' ');
  }
  DebugSerial.println("");
}

void decode512(can_frame &frame, CurrentEngineState &state)
{
  /*
  BO_ 512 BASE0: 8 Vector__XXX
   SG_ WarningCounter : 0|16@1+ (1,0) [0|0] "" Vector__XXX
   SG_ LastError : 16|16@1+ (1,0) [0|0] "" Vector__XXX
   SG_ RevLimAct : 32|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ MainRelayAct : 33|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ FuelPumpAct : 34|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ CELAct : 35|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ EGOHeatAct : 36|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ LambdaProtectAct : 37|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ Fan : 38|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ Fan2 : 39|1@1+ (1,0) [0|0] "" Vector__XXX
   SG_ CurrentGear : 40|8@1+ (1,0) [0|0] "" Vector__XXX
   SG_ DistanceTraveled : 48|16@1+ (0.1,0) [0|6553.5] "km" Vector__XXX
  */

  // Warning Counter: 16 bits, unsigned, scale=1, offset=0
  state.warningCounter = (uint16_t)(frame.data[0] | (frame.data[1] << 8));

  // Last Error: 16 bits, unsigned, scale=1, offset=0
  state.lastError = (uint16_t)(frame.data[2] | (frame.data[3] << 8));

  // Status bits are in bytes 4-5
  uint16_t statusBits = (uint16_t)(frame.data[4] | (frame.data[5] << 8));

  // Extract individual status bits
  state.revLimiterActive = (statusBits >> 0) & 0x01;    // bit 32
  state.mainRelayActive = (statusBits >> 1) & 0x01;     // bit 33
  state.fuelPumpActive = (statusBits >> 2) & 0x01;      // bit 34
  state.celActive = (statusBits >> 3) & 0x01;           // bit 35
  state.egoHeaterActive = (statusBits >> 4) & 0x01;     // bit 36
  state.lambdaProtectActive = (statusBits >> 5) & 0x01; // bit 37
  state.fanActive = (statusBits >> 6) & 0x01;           // bit 38
  state.fan2Active = (statusBits >> 7) & 0x01;          // bit 39
  // Current Gear: 8 bits, unsigned, scale=1, offset=0
  // state.currentGear = frame.data[5]; // Starting at bit 40, which is byte 5

  // Distance Traveled: 16 bits, unsigned, scale=0.1, offset=0
  // uint16_t rawDistance = (uint16_t)(frame.data[6] | (frame.data[7] << 8));
  // state->distanceTraveled = rawDistance * 0.1f;
}

void decode513(can_frame &frame, CurrentEngineState &state)
{

  // printFrame(frame);
  /*
  BO_ 513 BASE1: 8 Vector__XXX
    SG_ RPM : 0|16@1+ (1,0) [0|0] "RPM" Vector__XXX
    SG_ IgnitionTiming : 16|16@1- (0.02,0) [0|0] "deg" Vector__XXX
    SG_ InjDuty : 32|8@1+ (0.5,0) [0|100] "%" Vector__XXX
    SG_ IgnDuty : 40|8@1+ (0.5,0) [0|100] "%" Vector__XXX
    SG_ VehicleSpeed : 48|8@1+ (1,0) [0|255] "kph" Vector__XXX
    SG_ FlexPct : 56|8@1+ (1,0) [0|100] "%" Vector__XXX
  */
  // RPM: 16 bits, unsigned, scale=1, offset=0
  state.RPM = (uint16_t)(frame.data[0] | (frame.data[1] << 8));
  // Ignition Timing: 16 bits, signed, scale=0.02, offset=0
  int16_t rawTiming = (int16_t)(frame.data[2] | (frame.data[3] << 8));
  state.timing = rawTiming * 0.02f;

  // Injection Duty: 8 bits, unsigned, scale=0.5, offset=0
  // frame.injDuty = frame.data[4] * 0.5f;
  // Ignition Duty: 8 bits, unsigned, scale=0.5, offset=0
  // frame.ignDuty = frame.data[5] * 0.5f;
  // Vehicle Speed: 8 bits, unsigned, scale=1, offset=0
  // frame.vehicleSpeed = frame.data[6];
  // Flex Fuel Percentage: 8 bits, unsigned, scale=1, offset=0
  // frame.flexPct = frame.data[7];
}

void decode515(can_frame &frame, CurrentEngineState &state)
{
  /*

  BO_ 515 BASE3: 8 Vector__XXX
    SG_ MAP : 0|16@1+ (0.03333333,0) [0|0] "kPa" Vector__XXX
    SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|200] "deg C" Vector__XXX
    SG_ IntakeTemp : 24|8@1+ (1,-40) [-40|200] "deg C" Vector__XXX
    SG_ AUX1Temp : 32|8@1+ (1,-40) [-40|200] "deg C" Vector__XXX
    SG_ AUX2Temp : 40|8@1+ (1,-40) [-40|200] "deg C" Vector__XXX
    SG_ MCUTemp : 48|8@1+ (1,-40) [-40|100] "deg C" Vector__XXX
    SG_ FuelLevel : 56|8@1+ (0.5,0) [0|0] "%" Vector__XXX
  */
  // Decode MAP (16 bits, scale 0.03333333, offset 0)
  uint16_t map_raw = (uint16_t)(frame.data[0] | (frame.data[1] << 8));
  float map_kpa = map_raw * 0.03333333f;
  // Decode CoolantTemp (8 bits, scale 1, offset -40)
  state.coolantTemp = frame.data[2] - 40;
  // Decode IntakeTemp (8 bits, scale 1, offset -40)
  state.iat = frame.data[3] - 40;

  // Decode AUX1Temp (8 bits, scale 1, offset -40)
  // int8_t aux1_temp = frame.data[4] - 40;

  // Decode AUX2Temp (8 bits, scale 1, offset -40)
  // int8_t aux2_temp = frame.data[5] - 40;

  // Decode MCUTemp (8 bits, scale 1, offset -40)
  // int8_t mcu_temp = frame.data[6] - 40;

  // TOOD: plumb this into the ADC?
  // Decode FuelLevel (8 bits, scale 0.5, offset 0)
  // float fuel_level = frame.data[7] * 0.5f;
}

void decode516(can_frame &frame, CurrentEngineState &state)
{
  // printFrame(frame);
  /*

    BO_ 516 BASE4: 8 Vector__XXX
      SG_ OilPress : 16|16@1+ (0.03333333,0) [0|0] "kPa" Vector__XXX
      SG_ OilTemperature : 32|8@1+ (1,-40) [-40|215] "deg C" Vector__XXX
      SG_ FuelTemperature : 40|8@1+ (1,-40) [-40|215] "deg C" Vector__XXX
      SG_ BattVolt : 48|16@1+ (0.001,0) [0|25] "mV" Vector__XXX
  */

  // Decode OilPress (16 bits, scale 0.03333333, offset 0)
  uint16_t oil_press_raw = (uint16_t)(frame.data[2] | (frame.data[3] << 8));
  state.oilPressure = oil_press_raw * 0.03333333f;

  // Decode OilTemperature (8 bits, scale 1, offset -40)
  state.oilTemp = frame.data[4] - 40;

  // Decode FuelTemperature (8 bits, scale 1, offset -40)
  // int8_t fuel_temp = data[5] - 40;

  // Decode BattVolt (16 bits, scale 0.001, offset 0)
  uint16_t batt_volt_raw = (uint16_t)(frame.data[6] | (frame.data[7] << 8));
  state.volts = batt_volt_raw * 0.001f;
}

void decode517(can_frame &frame, CurrentEngineState &state)
{
  /*
  BO_ 517 BASE5: 8 Vector__XXX
   SG_ CylAM : 0|16@1+ (1,0) [0|0] "mg" Vector__XXX
   SG_ EstMAF : 16|16@1+ (0.01,0) [0|0] "kg/h" Vector__XXX
   SG_ InjPW : 32|16@1+ (0.003333333,0) [0|0] "ms" Vector__XXX
   SG_ KnockCt : 48|16@1+ (1,0) [0|0] "count" Vector__XXX
  */

  // Decode CylAM (16 bits, scale 1, offset 0)
  // uint16_t cyl_am_mg = (uint16_t)(data[0] | (data[1] << 8));

  // Decode EstMAF (16 bits, scale 0.01, offset 0)
  // uint16_t est_maf_raw = (uint16_t)(data[2] | (data[3] << 8));
  // float est_maf_kgh = est_maf_raw * 0.01f;

  // Decode InjPW (16 bits, scale 0.003333333, offset 0)
  // uint16_t inj_pw_raw = (uint16_t)(data[4] | (data[5] << 8));
  // float inj_pw_ms = inj_pw_raw * 0.003333333f;

  // Decode KnockCt (16 bits, scale 1, offset 0)
  state.knockCount = (uint16_t)(frame.data[6] | (frame.data[7] << 8));
}

void decode518(can_frame &frame, CurrentEngineState &state)
{
  /*
  BO_ 518 BASE6: 8 Vector__XXX
   SG_ FuelUsed : 0|16@1+ (1,0) [0|0] "g" Vector__XXX
   SG_ FuelFlow : 16|16@1+ (0.005,0) [0|327] "g/s" Vector__XXX
   SG_ FuelTrim1 : 32|16@1+ (0.01,0) [-50|50] "%" Vector__XXX
   SG_ FuelTrim2 : 48|16@1+ (0.01,0) [-50|50] "%" Vector__XXX
  */
  // FuelUsed: 16 bits, unsigned, scale=1, offset=0
  state.fuelUsed = (uint16_t)(frame.data[0] | (frame.data[1] << 8));

  // // FuelFlow: 16 bits, unsigned, scale=0.005, offset=0
  // uint16_t rawFuelFlow = (uint16_t)(frame.data[2] | (frame.data[3] << 8));
  // state.fuelFlow = rawFuelFlow * 0.005f;

  // // FuelTrim1: 16 bits, signed, scale=0.01, offset=0
  // int16_t rawTrim1 = (int16_t)(frame.data[4] | (frame.data[5] << 8));
  // state.fuelTrim1 = rawTrim1 * 0.01f;

  // // FuelTrim2: 16 bits, signed, scale=0.01, offset=0
  // int16_t rawTrim2 = (int16_t)(frame.data[6] | (frame.data[7] << 8));
  // state.fuelTrim2 = rawTrim2 * 0.01f;
}

void decode519(const can_frame &frame, CurrentEngineState &state)
{
  /*
  BO_ 519 BASE7: 8 Vector__XXX
   SG_ Lam1 : 0|16@1+ (0.0001,0) [0|2] "lambda" Vector__XXX
   SG_ Lam2 : 16|16@1+ (0.0001,0) [0|2] "lambda" Vector__XXX
   SG_ FpLow : 32|16@1+ (0.03333333,0) [0|0] "kPa" Vector__XXX
   SG_ FpHigh : 48|16@1+ (0.1,0) [0|0] "bar" Vector__XXX
  */

  // Lambda 1: 16 bits, unsigned, scale=0.0001, offset=0
  uint16_t rawLam1 = (uint16_t)(frame.data[0] | (frame.data[1] << 8));
  state.lambda = rawLam1 * 0.0001f;

  // // Lambda 2: 16 bits, unsigned, scale=0.0001, offset=0
  // uint16_t rawLam2 = (uint16_t)(frame.data[2] | (frame.data[3] << 8));
  // state.lambda2 = rawLam2 * 0.0001f;

  // FpLow: 16 bits, unsigned, scale=0.03333333, offset=0
  uint16_t rawFpLow = (uint16_t)(frame.data[4] | (frame.data[5] << 8));
  state.fuelPressure = rawFpLow * 0.03333333f;

  // FpHigh: 16 bits, unsigned, scale=0.1, offset=0
  // uint16_t rawFpHigh = (uint16_t)(frame.data[6] | (frame.data[7] << 8));
  // state.fuelPressureHigh = rawFpHigh * 0.1f;
}

void decodeMessage(const can_frame &frame, CurrentEngineState &state)
{
  memset(state.message, 0, 8);

  memcpy(state.message, frame.data, frame.can_dlc);
  state.message[frame.can_dlc] = NULL;
  state.messageAppearedAt = millis();
}

void parseMessage(can_frame &frame, CurrentEngineState &state)
{
  if (frame.can_dlc > 0)
  {
    // printFrame(frame);
    switch (frame.can_id)
    {
    case 508:
      decodeMessage(frame, state);
      break;

    case 513:
      decode513(frame, state);
      break;
    case 515:
      decode515(frame, state);
      break;
    case 516:
      decode516(frame, state);
      break;
    case 517:
      decode517(frame, state);
      break;
    case 518:
      decode518(frame, state);
      break;
    case 519:
      decode519(frame, state);
      break;
    default:
      return;
    }
  }
}

/*
  Read at most MAX_MSG_COUNT from the mcp
  and update the global state
  return true if a re-render is explicitly requested
*/
long lastUpdate = 0;
bool updateState(CurrentEngineState &state)
{
  if (frameCount > 0)
  {
    for (int i = 0; i < CAN_FRAME_BUFFER_LEN; i++)
    {
      DebugSerial.printf("can_id=%d dlc=%d, ", frames[i].can_id, frames[i].can_dlc);
      if (frames[i].can_id >= CAN_MIN_ADDRESS && frames[i].can_id <= CAN_MAX_ADDRESS)
      {
        parseMessage(frames[i], state);
      }
    }
    DebugSerial.println();
    lastUpdate = millis();
    // we've consumer the frames - kill them all
    memset(&frames, 0, sizeof(frames));

    state.canState = MCP2515::ERROR_OK;
    state.messageCount = state.messageCount + frameCount;
    state.missedMessageCount = 0;
    frameCount = 0;
    return true;
    // DebugSerial.printf("irq=%x isrx0=%d isrx1=%d\n", irq, irq & MCP2515::CANINTF_RX0IF, irq & MCP2515::CANINTF_RX1IF);
  }
  else
  {
    long now = millis();
    if (now - lastUpdate > 1000)
    {
      DebugSerial.println("no msg");
      state.canState = MCP2515::ERROR_NOMSG;
      state.missedMessageCount++;
      lastUpdate = now;
    }
    return true;
  }

  return false;
}

void sendFuelPctOverCan(int fuelPct) {
  struct can_frame frame;
  
  frame.can_id = 420;
  frame.can_dlc = 1; // 1 byte for fuel percentage 0-100
  
  frame.data[0] = fuelPct & 0xFF;
  
  // Clear remaining bytes
  for (int i = 1; i < 8; i++) {
    frame.data[i] = 0;
  }
  
  // Send the frame
  MCP2515::ERROR sendResult = mcp2515.sendMessage(&frame);
  
  if (sendResult != MCP2515::ERROR_OK) {
    DebugSerial.print("Failed to send fuel percentage over CAN: ");
    DebugSerial.println(sendResult);
  }
}
