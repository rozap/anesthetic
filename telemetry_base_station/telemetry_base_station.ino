// -*- mode: C++ -*-
#include <SPI.h>
#include <RH_RF95.h>

RH_RF95 rf95(10, 2);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available
  if (rf95.init()) {
    rf95.setTxPower(20, false);
    Serial.println("init ok");
  } else {
    Serial.println("init failed");
  }
}

bool led_on = false;
void loop() {
  if (rf95.waitAvailableTimeout(300)) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN + 1];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len)) {
      buf[len] = 0;
      Serial.print((char*)buf);
      Serial.print("RSI:");
      Serial.println(rf95.lastRssi(), DEC);
      digitalWrite(LED_BUILTIN, led_on);
      led_on = !led_on;
    }
  }

  delay(50);
}
