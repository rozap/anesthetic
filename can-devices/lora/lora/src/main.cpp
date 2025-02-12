#include <Arduino.h>
#include <mcp2515.h>

/* Radio */
// Some vestigial RadioHead stuff we can refactor out later.
// RadioHead header compat
#define RH_RF95_HEADER_LEN 4
#define RH_BROADCAST_ADDRESS 0xff

// Max number of octets the LORA Rx/Tx FIFO can hold
#define RH_RF95_FIFO_SIZE 255

// This is the maximum number of bytes that can be carried by the LORA.
// We use some for headers, keeping fewer for RadioHead messages
#define RH_RF95_MAX_PAYLOAD_LEN RH_RF95_FIFO_SIZE

// This is the maximum message length that can be supported by this driver.
// Can be pre-defined to a smaller size (to save SRAM) prior to including this header
// Here we allow for 1 byte message length, 4 bytes headers, user data and 2 bytes of FCS
#define RH_RF95_MAX_MESSAGE_LEN (RH_RF95_MAX_PAYLOAD_LEN - RH_RF95_HEADER_LEN)

#include <LoRa_STM32.h>

#define DEBUG true

HardwareSerial DebugSerial = Serial1;

SPIClass mpcSPI(PA7, PA6, PA5);
MCP2515 mcp2515(PA4);

// PC14 goes to LoRa D2
// PB5  goes to LoRa D9
// PB12 goes to LoRa D10
// PB15 goes to LoRa D11
// PB14 goes to LoRa D12
// PB13 goes to LoRa D13

SPIClass radioSPI(PB15, PB14, PB13); //, PB12);
bool radioAvailable;
uint8_t radioPktSpaceLeft;


//https://github.com/FOME-Tech/fome-fw/blob/master/firmware/controllers/can/FOME_CAN_verbose.dbc

void setup()
{

  DebugSerial.begin(9600);

  mpcSPI.begin();
  mcp2515.reset();
  mcp2515.setBitrate(CAN_125KBPS);
  mcp2515.setNormalMode();

  DebugSerial.println("init radio");
  radioSPI.begin();
  LoRa.setPins(PB12 /* CS */, PB5 /* Reset */, PC14 /* IRQ */);
  radioAvailable = LoRa.begin(915E6, false /* useLNA */, &radioSPI);

  if (radioAvailable)
  {
    DebugSerial.println("radio init ok");

    LoRa.setSignalBandwidth(250E3);
    LoRa.setCodingRate4(5);
    LoRa.setSpreadingFactor(7);
    LoRa.setPreambleLength(8);
    LoRa.enableCrc();
    LoRa.setTxPower(20);
  }
  else
  {
    DebugSerial.println("radio init failed");
  }
}

// 4 bytes for the id
// 1 byte for dlc
// 8 bytes for content
#define FRAMES_PER_PACKET 19
bool initialSend = true;
uint8_t packetIndex = 0;
struct can_frame canFrames[FRAMES_PER_PACKET];
struct can_frame currentFrame;
// 19 can frames can fit in the fifo

void flushFrames()
{
  LoRa.beginPacket();
  for (int i = 0; i < FRAMES_PER_PACKET; i++)
  {
    LoRa.write(canFrames[i].can_id);
    LoRa.write(canFrames[i].can_dlc);
    for (int i = 0; i < canFrames[i].can_dlc; i++)
    {
      LoRa.write(canFrames[i].data[i]);
    }
  }
  LoRa.endPacket(true);
}

void loop()
{

  MCP2515::ERROR res = mcp2515.readMessage(&canFrames[packetIndex]);
  if (res == MCP2515::ERROR_OK)
  {
    packetIndex++;
    DebugSerial.println("Got a CAN packet");
    if (packetIndex == (FRAMES_PER_PACKET - 1))
    {
      // if the radio is busy we'll just start overwriting 
      // can frames and in the beginning of the window
      // and hopefully send stuff on the next go-round
      if (LoRa.isAsyncTxDone() || initialSend)
      {
        initialSend = false;
        flushFrames();
      }
      packetIndex = 0;
    }
  } else {
    DebugSerial.printf("Got CAN res: %d\n", res);
  }
}
