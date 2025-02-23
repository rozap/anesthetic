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
  uint16_t warningCounter;
  uint16_t lastError;

  bool fanOn;
  bool revLimiterActive;
  bool mainRelayActive;
  bool fuelPumpActive;
  bool celActive;
  bool egoHeaterActive;

  bool lambdaProtectActive;
  bool fanActive;
  bool fan2Active;

  uint16_t missedMessageCount;
  MCP2515::ERROR canState;
};