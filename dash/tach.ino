#include "Wire.h"

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

void tachConfig(uint8_t configByte) {
  Wire.beginTransmission(SAA_ADDR);
  Wire.write(SAA_ADDR_CONTROL);
  Wire.write(configByte | SAA_SEGMENT_MODE);
  Wire.endTransmission();
}

void tachInit() {
  tachConfig(SAA_BRIGHTNESS);
}

void tachLights(uint16_t lights) {
  tachInit(); // In case someone unplugged the wire. Cheap to re-init anyway.
  Wire.beginTransmission(SAA_ADDR);
  Wire.write(SAA_ADDR_DIGIT_1);
  Wire.write((lights >> 8) & 0xff);
  Wire.write(lights & 0xff);
  Wire.endTransmission();
}

void tachBootAnimation() {
  uint8_t d = 150;

  tachLights(0); delay(d);
  tachLights(TACH_LIGHT_Y1 | TACH_LIGHT_Y2); delay(d);
  tachLights(TACH_LIGHT_G3 | TACH_LIGHT_Y3); delay(d);
  tachLights(TACH_LIGHT_G2 | TACH_LIGHT_R1); delay(d);
  tachLights(TACH_LIGHT_G1 | TACH_LIGHT_R2); delay(d);

  delay(500);
  tachLights(0);
}

void updateTach(uint16_t rpm, bool idiotLight) {
  uint16_t lights = 0;
  if (idiotLight && ((millis() % 100) > 50)) { lights |= TACH_LIGHT_IDIOT; }

  if (rpm >= REDLINE_RPM) {
    // Blink red lights! Hope the engine survived.
    // TODO: Think about how to cut spark.
    if (millis() % 100 > 50) {
      lights |= (TACH_LIGHT_R1 | TACH_LIGHT_R2);
    }
  } else if (rpm >= FIRST_LIGHT_RPM) {
    // Fallthrough intentional.
    switch(map(rpm, FIRST_LIGHT_RPM, REDLINE_RPM, 1, 8)) {
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
  tachLights(lights);
}
