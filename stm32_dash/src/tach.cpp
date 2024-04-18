#include <Wire.h>
#include "tach.h"

#define SAA_ADDR (0x70 >> 1)
#define SAA_DYNAMIC (1 << 0)
#define SAA_STATIC (0)
#define SAA_DIGIT_2_4_UNBLANK (1 << 1)
#define SAA_DIGIT_1_3_UNBLANK (1 << 2)
#define SAA_SEG_TEST (1 << 3)
#define SAA_SEG_03MA (1 << 4)
#define SAA_SEG_06MA (1 << 5)
#define SAA_SEG_12MA (1 << 6)

#define SAA_ADDR_CONTROL 0
#define SAA_ADDR_DIGIT_1 1

#define SAA_BRIGHTNESS (SAA_SEG_03MA)
#define SAA_SEGMENT_MODE (SAA_STATIC | SAA_DIGIT_1_3_UNBLANK)

#define TACH_LIGHT_IDIOT (1 << 8)
#define TACH_LIGHT_G1 (1 << 13)
#define TACH_LIGHT_G2 (1 << 12)
#define TACH_LIGHT_G3 (1 << 9)
#define TACH_LIGHT_Y1 (1 << 15)
#define TACH_LIGHT_Y2 (1 << 10)
#define TACH_LIGHT_Y3 (1 << 11)
#define TACH_LIGHT_R1 (1 << 0)
#define TACH_LIGHT_R2 (1 << 14)

// I2C 2
//            SDA  SCL
TwoWire TachI2C(PB3, PB10);

uint16_t lastDisplayedLights;

// Private

void tachConfig(uint8_t configByte) {
  TachI2C.beginTransmission(SAA_ADDR);
  TachI2C.write(SAA_ADDR_CONTROL);
  TachI2C.write(configByte | SAA_SEGMENT_MODE);
  TachI2C.endTransmission();
}



void tachLights(uint16_t lights) {
  TachI2C.beginTransmission(SAA_ADDR);
  TachI2C.write(SAA_ADDR_DIGIT_1);
  TachI2C.write((lights >> 8) & 0xff);
  TachI2C.write(lights & 0xff);
  TachI2C.endTransmission();
}

// Public

void tachDisplayInit() {
  TachI2C.begin();
  tachConfig(SAA_BRIGHTNESS);
  lastDisplayedLights = 0;
}

void clearTachLights() {
  tachLights(0);
}

void updateTach(uint16_t rpm, uint16_t firstLightRpm, uint16_t redlineRpm, bool idiotLight) {
  uint16_t lights = 0;
  if (idiotLight && ((millis() % 100) > 50)) { lights |= TACH_LIGHT_IDIOT; }

  if (rpm >= redlineRpm) {
    // Blink red lights! Hope the engine survived.
    // TODO: Think about how to cut spark.
    if (millis() % 100 > 50) {
      lights |= (TACH_LIGHT_R1 | TACH_LIGHT_R2);
    }
  } else if (rpm >= firstLightRpm) {
    // Fallthrough intentional.
    switch(map(rpm, firstLightRpm, redlineRpm, 1, 8)) {
      case 8: lights |= TACH_LIGHT_R2;
      case 7: lights |= TACH_LIGHT_R1;
      case 6: lights |= TACH_LIGHT_Y3;
      case 5: lights |= TACH_LIGHT_Y2;
      case 4: lights |= TACH_LIGHT_Y1;
      case 3: lights |= TACH_LIGHT_G3;
      case 2: lights |= TACH_LIGHT_G2;
      case 1: lights |= TACH_LIGHT_G1;
    }
  }

  if (lights != lastDisplayedLights) {
    tachLights(lights);
    lastDisplayedLights = lights;
  }
}

void tachBootAnimation() {
  uint8_t d = 150;

  tachLights(0); delay(d);
  tachLights(TACH_LIGHT_Y1 | TACH_LIGHT_Y2); delay(d);
  tachLights(TACH_LIGHT_G3 | TACH_LIGHT_Y3); delay(d);
  tachLights(TACH_LIGHT_G2 | TACH_LIGHT_R1); delay(d);
  tachLights(TACH_LIGHT_G1 | TACH_LIGHT_R2); delay(d);
  tachLights(TACH_LIGHT_IDIOT); delay(d);

  delay(500);
  tachLights(0);
}
