#pragma once
#ifndef STATE_H
#define STATE_H
extern HardwareSerial& DebugSerial;

struct CurrentEngineState
{
  uint16_t RPM;
  uint16_t timing;
  double oilPressure;
  int coolantTemp;
  int iat;
  double volts;
  double lambda;
  uint16_t oilTemp;
  double fuelPressure;
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
  uint16_t messageCount;
  MCP2515::ERROR canState;
};
#endif