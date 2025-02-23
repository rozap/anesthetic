#pragma once

struct CurrentEngineState
{
  uint16_t RPM;
  uint16_t timing;
  float oilPressure;
  uint16_t coolantTemp;
  float volts;
  float lambda;
  uint16_t oilTemp;
  uint16_t fuelPressure;
  uint16_t iat;
  uint16_t fuelUsed;
  uint16_t knockCount;

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

  uint16_t missedMessageCount;
  MCP2515::ERROR canState;
};