// -*- mode: C++ -*-
#include <SPI.h>
#include <RH_RF95.h>

#define LEDPIN 3

RH_RF95 rf95(10, 2);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LEDPIN, OUTPUT);
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available
  if (rf95.init()) {
    rf95.setTxPower(20, false);
    rf95.setSignalBandwidth(250E3);
    Serial.println("init ok");
  } else {
    Serial.println("init failed");
  }
}

void loop() {
  if (rf95.waitAvailableTimeout(300)) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN + 1];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len)) {
      buf[len] = 0;
      Serial.println((char*)buf);
      Serial.print("RSI:");
      Serial.println(rf95.lastRssi(), DEC);
      digitalWrite(LED_BUILTIN, HIGH);
      digitalWrite(LEDPIN, HIGH);
    }
  }

  delay(20);
  
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LEDPIN, LOW); 
}
