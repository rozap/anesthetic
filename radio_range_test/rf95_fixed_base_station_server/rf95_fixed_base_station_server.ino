// -*- mode: C++ -*-
// Range test instructions: Run this sketch on a radio. It will sit there looking for messages.
// It will reply when it gets one. Flash the client sketch on another radio and move around with it.

// This sketch needs the RadioHead library to be installed in your Arduino environment. See the repo
// top-level README for instructions.

#include <SPI.h>
#include <RH_RF95.h>

RH_RF95 rf95(10, 2);
uint8_t c;

void setup() {
  c = 0;
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available
  if (rf95.init()) {
    rf95.setTxPower(20, false);
    Serial.println("init ok");
  } else {
    Serial.println("init failed");
  }
}

void loop() {
  if (rf95.available()) {
    uint8_t data[] = "And hello back to you";
    rf95.send(data, sizeof(data));
    rf95.waitPacketSent();
    Serial.println("Sent a reply");

    return
    // uint8_t buf[RH_RF95_MAX_MESSAGE_LEN + 1];
    // uint8_t len = sizeof(buf);
    // if (rf95.recv(buf, &len)) {
    //   Serial.println("====");
    //   Serial.println(c++, DEC);
    //   Serial.print("got request: ");
    //   buf[len] = 0;
    //   Serial.println((char*)buf);
    //   Serial.print("RSSI: ");
    //   Serial.println(rf95.lastRssi(), DEC);

    // } else {
    //   Serial.println("recv failed");
    // }
  }
}

