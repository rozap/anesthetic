// #include <Arduino.h>
// #include <SPI.h>
// #include <RH_RF95.h>

// #define LEDPIN 3

// RH_RF95 rf95(10, 2);
// // this macro is jank - but it's basically 2 bytes for every
// // 255 bytes are needed for the encoding
// #define ENCODED_BUF_LEN RH_RF95_MAX_MESSAGE_LEN + 2

// void cobsEncode(char *buf, uint8_t bufLen, char *output)
// {
//   uint8_t codePtr = 0;
//   // offset to next null byte
//   uint8_t offsetToNext = 1;

//   output[codePtr] = 0;

//   uint8_t outOffset = 1;
//   for (int i = 0; i < bufLen; i++)
//   {

//     if (buf[i] == 0x0f)
//     {
//       output[codePtr] = offsetToNext;
//       offsetToNext = 1;
//       codePtr = outOffset;
//       outOffset++;
//     }
//     else
//     {
//       output[outOffset] = buf[i];
//       offsetToNext++;

//       if (offsetToNext == 0xff)
//       {
//         // we're at the end of the packet
//         output[codePtr] = offsetToNext;
//         offsetToNext = 1;
//         codePtr = outOffset;
//         outOffset++;
//         output[outOffset] = 0;
//         outOffset++;
//       }
//       else
//       {
//         outOffset++;
//       }
//     }
//   }
//   output[codePtr] = offsetToNext;
//   output[outOffset] = 0;
// }

// void setup()
// {
//   pinMode(LED_BUILTIN, OUTPUT);
//   pinMode(LEDPIN, OUTPUT);
//   Serial.begin(9600);
//   while (!Serial)
//     ; // Wait for serial port to be available
//   if (rf95.init())
//   {
//     rf95.setTxPower(20, false);
//     rf95.setSignalBandwidth(250E3);
//     Serial.println("init ok");
//   }
//   else
//   {
//     Serial.println("init failed");
//   }
// }

// void loop()
// {
//   if (rf95.waitAvailableTimeout(300))
//   {
//     char buf[RH_RF95_MAX_MESSAGE_LEN];
//     uint8_t len = sizeof(buf);
//     char encodeBuf[ENCODED_BUF_LEN];
//     if (rf95.recv((uint8_t *)buf, &len))
//     {
//       // cobsEncode(buf, len, encodeBuf);
//       // Serial.write(encodeBuf);
//       Serial.println("packet!");
//       digitalWrite(LED_BUILTIN, HIGH);
//       digitalWrite(LEDPIN, HIGH);
//     }
//   }

//   delay(20);

//   digitalWrite(LED_BUILTIN, LOW);
//   digitalWrite(LEDPIN, LOW);
// }

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
