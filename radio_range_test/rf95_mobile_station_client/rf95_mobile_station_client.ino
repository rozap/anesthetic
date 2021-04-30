// -*- mode: C++ -*-
// Range test instructions: Run this sketch on a radio attached to a laptop.
// Run the other sketch (rf95_fixed_base_station_server) on a fixed radio
// (leave this one unattended). Take your radio and laptop around and watch
// the serial output.

// This sketch needs the RadioHead library to be installed in your Arduino environment. See the repo
// top-level README for instructions.

#include <SPI.h>
#include <RH_RF95.h>

RH_RF95 rf95;
uint8_t c;

void setup() {
  c = 0;
  Serial.begin(57600);
  while (!Serial) ; // Wait for serial port to be available
  if (rf95.init()) {
    rf95.setTxPower(20, false);
  } else {
    Serial.println("init failed");
  }
}

void loop() {
  uint8_t data[] = "Hello World!";
  rf95.send(data, sizeof(data));

  rf95.waitPacketSent();
  // Now wait for a reply
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  if (rf95.waitAvailableTimeout(3000)) {
    if (rf95.recv(buf, &len)) {
      Serial.println("====");
      Serial.println(c++, DEC);
      Serial.print("got reply: ");
      Serial.println((char*)buf);
      Serial.print("RSSI: ");
      Serial.println(rf95.lastRssi(), DEC);
    } else {
      Serial.println("recv failed");
    }
  } else {
    //Serial.println("No reply, is rf95_server running?");
  }
  delay(1000);
}

