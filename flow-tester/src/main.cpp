#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "OneButton.h"
#include "Ticker.h"

#define USE_TIMER_1 true
#define USE_TIMER_2 false
#define USE_TIMER_3 false
#define USE_TIMER_4 false
#define USE_TIMER_5 false

LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 16 chars and 2 line display

#define SET_TIME_PIN 9
#define SET_RPM_PIN 8
#define UP_PIN 2
#define DOWN_PIN 3
#define RUN_PIN 4

#define CYL_1_PIN 10
#define CYL_2_PIN 11
#define CYL_3_PIN 12
#define FUEL_RELAY_PIN 13

#define PRIME_MILLIS 3 * 1000

OneButton upButton = OneButton(UP_PIN, true, true);
OneButton downButton = OneButton(DOWN_PIN, true, true);
OneButton runButton = OneButton(RUN_PIN, true, true);

uint16_t runTime = 30 * 1000;
uint16_t rpm = 1000;
uint8_t duty = 50;

uint16_t timeRemaining = 0;
bool running = false;

#define MIN_RPM 500
#define MAX_RPM 6500
#define TIME_DELTA 2000
#define DUTY_DELTA 10
#define MAX_DUTY 100

void HandleDown(void *b);
void HandleUp(void *b);
void HandleRun(void *b);

unsigned long lastTick;
unsigned long lastRender;
bool render = true;

void setup()
{

  lastTick = millis();
  lastRender = millis();

  lcd.init();
  lcd.backlight();
  Serial.begin(9600);

  upButton.attachClick(HandleUp, &upButton);
  downButton.attachClick(HandleDown, &downButton);
  runButton.attachClick(HandleRun, &runButton);
  upButton.setClickMs(100);
  downButton.setClickMs(100);
  runButton.setClickMs(100);

  pinMode(SET_TIME_PIN, INPUT_PULLUP);
  pinMode(SET_RPM_PIN, INPUT_PULLUP);

  pinMode(CYL_1_PIN, OUTPUT);
  pinMode(CYL_2_PIN, OUTPUT);
  pinMode(CYL_3_PIN, OUTPUT);
  pinMode(FUEL_RELAY_PIN, OUTPUT);

  digitalWrite(CYL_1_PIN, HIGH);
}

bool isPressed(uint8_t pin)
{
  return digitalRead(pin) == 0;
}

bool isSetRPMMode()
{
  return isPressed(SET_RPM_PIN);
}
bool isSetTimeMode()
{
  return isPressed(SET_TIME_PIN);
}

void HandleUp(void *b)
{
  render = true;
  Serial.println("on up");
  if (isSetRPMMode())
  {
    rpm += 500;
    if (rpm > MAX_RPM)
    {
      rpm = MIN_RPM;
    }
    return;
  }
  if (isSetTimeMode())
  {
    runTime += 500;
    return;
  }
  else
  {
    duty += DUTY_DELTA;
    if (duty > MAX_DUTY)
    {
      duty = 0;
    }
  }
}

void HandleDown(void *b)
{
  Serial.println("on down");
  render = true;

  if (isSetRPMMode())
  {
    rpm -= TIME_DELTA;
    if (rpm < MIN_RPM)
    {
      rpm = MAX_RPM;
    }
  }
  else if (isSetTimeMode())
  {
    runTime -= TIME_DELTA;
  }
  else
  {
    duty -= DUTY_DELTA;
    if (duty < 0)
    {
      duty = MAX_DUTY;
    }
  }
}

uint8_t InjectorPins[3] = {
    CYL_1_PIN,
    CYL_2_PIN,
    CYL_3_PIN};

struct InjectorStatus
{
  unsigned int onTime;
  unsigned int offTime;
  unsigned int cycleTime; // how long one revolution takes
  bool state[3];
};

InjectorStatus status;

void fire1();
void fire2();
void fire3();
Ticker ticker1(fire1, 0, 0, MILLIS);
Ticker ticker2(fire2, 0, 0, MILLIS);
Ticker ticker3(fire3, 0, 0, MILLIS);

void fire(uint8_t which, Ticker *ticker)
{
  char buffer[16];
  if (status.state[which])
  {
    snprintf(buffer, sizeof(buffer), "Off: %d for %d", which, status.offTime);

    // turn injector off
    digitalWrite(InjectorPins[which], LOW);
    status.state[which] = false;
    ticker->interval(status.offTime);
  }
  else
  {
    snprintf(buffer, sizeof(buffer), "On: %d for %d", which, status.onTime);

    digitalWrite(InjectorPins[which], HIGH);
    status.state[which] = true;
    ticker->interval(status.onTime);
  }
  Serial.println(buffer);
}

void fire1() { fire(0, &ticker1); };
void fire2() { fire(1, &ticker2); };
void fire3() { fire(2, &ticker3); };

float computeCycleTime()
{
  return 1 / ((rpm / 60.0) / 1000.0);
}

unsigned int computeOnTime()
{
  return computeCycleTime() * (duty / 100.0);
}
unsigned int computeOffTime()
{
  return computeCycleTime() - computeOnTime();
}

void stopRunning()
{
  render = true;

  digitalWrite(InjectorPins[0], LOW);
  digitalWrite(InjectorPins[1], LOW);
  digitalWrite(InjectorPins[2], LOW);
  digitalWrite(FUEL_RELAY_PIN, LOW);

  running = false;
  timeRemaining = 0;
}

void startRunning()
{
  lcd.clear();
  lcd.print("Prime...");
  digitalWrite(FUEL_RELAY_PIN, HIGH);
  delay(PRIME_MILLIS);

  render = true;

  running = true;
  timeRemaining = runTime + PRIME_MILLIS;

  float cycleTime = computeCycleTime();
  unsigned int onTime = computeOnTime();

  ticker1 = Ticker(fire1, 0, 0xFFFFFFFF, MILLIS);
  ticker2 = Ticker(fire2, (unsigned int)(cycleTime * 0.33), 0xFFFFFFFF, MILLIS);
  ticker3 = Ticker(fire3, (unsigned int)(cycleTime * 0.66), 0xFFFFFFFF, MILLIS);

  ticker1.start();
  ticker2.start();
  ticker3.start();

  status = {
      onTime,
      computeOffTime(),
      (unsigned int)cycleTime,
      {false, false, false}};
}

void HandleRun(void *b)
{
  Serial.println("on run");
  if (running)
  {
    stopRunning();
  }
  else
  {
    startRunning();
  }
}

char timeStr[20];
char rpmStr[6];

void loop()
{
  unsigned long now = millis();
  unsigned long elapsed = now - lastTick;
  lastTick = now;

  upButton.tick();
  downButton.tick();
  runButton.tick();

  if (now - lastRender > 500)
  {
    lastRender = now;
    render = true;
  }
  else
  {
    render = false;
  }

  if (running)
  {

    if (timeRemaining < elapsed)
    {
      stopRunning();
      return;
    }
    timeRemaining -= elapsed;

    ticker1.update();
    ticker2.update();
    ticker3.update();

    if (render)
    {
      lcd.clear();
      lcd.print("Running:");
      dtostrf(timeRemaining / 1000.0, 0, 1, timeStr);
      lcd.print(timeStr);
      lcd.setCursor(0, 1);
      lcd.print("ON:");
      lcd.print(status.onTime);
      lcd.print(" CYC:");
      lcd.print((unsigned int)computeCycleTime());
    }
  }
  else
  {
    if (render)
    {

      lcd.clear();
      if (isSetRPMMode())
      {
        lcd.print("Set RPM");
      }
      else if (isSetTimeMode())
      {
        lcd.print("Set Time");
      }
      else
      {
        lcd.print("Ready ");
      }

      lcd.print("Duty:");
      lcd.print(duty);
      lcd.setCursor(0, 1);
      lcd.print("T:");
      dtostrf(runTime / 1000.0, 0, 1, timeStr);
      lcd.print(timeStr);
      lcd.print("s ");
      lcd.print("RPM:");
      lcd.print(rpm);
      render = false;
    }
  }
}
