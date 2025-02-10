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

// On mcp2515
// A7 -> MOSI
// A6 -> MISO
// A5 -> SCK
// A4 -> CS
//
// On lora (powered by 3.3v)
// PC14 goes to LoRa D2
// PB5  goes to LoRa D9
// PB12 goes to LoRa D10
// PB15 goes to LoRa D11
// PB14 goes to LoRa D12
// PB13 goes to LoRa D13

SPIClass radioSPI(PB15, PB14, PB13); //, PB12);
bool radioAvailable;
uint8_t radioPktSpaceLeft;

// https://github.com/FOME-Tech/fome-fw/blob/master/firmware/controllers/can/FOME_CAN_verbose.dbc

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
#define FLOOR_DIV(x, y) ((x) / (y) - ((x) % (y) < 0 ? 1 : 0))
#define FRAMES_PER_PACKET 3
bool initialSend = true;
uint8_t packetIndex = 0;
struct can_frame canFrames[FRAMES_PER_PACKET];
struct can_frame currentFrame;
// 19 can frames can fit in the fifo

void loraSendDummyRadioHeadHeader()
{
  // Stub implementation of the header the RadioHead library uses.
  // It conveys source and destination addresses as well as extra
  // packet flags. We don't need these in our dash implementation,
  // but since the base station uses RadioHead let's just make it
  // happy. Perhaps we should consolidate libraries in the future.
  // NB: If we ever need to _read_ incoming packets, discard the
  // first 4 incoming bytes.
  LoRa.write(RH_BROADCAST_ADDRESS); // Header: TO
  LoRa.write(RH_BROADCAST_ADDRESS); // Header: FROM
  LoRa.write(0);                    // Header: ID
  LoRa.write(0);                    // Header: FLAGS
}

void flushFrames()
{
  LoRa.beginPacket();
  loraSendDummyRadioHeadHeader();

  for (int i = 0; i < FRAMES_PER_PACKET; i++)
  {
    // can_id is 4 bytes
    LoRa.write((uint8_t *)&canFrames[i].can_id, 4);
    // dlc just one byte
    LoRa.write(canFrames[i].can_dlc);
    // actual payload is dlc length
    LoRa.write(canFrames[i].data, canFrames[i].can_dlc);

    DebugSerial.write((uint8_t *)&canFrames[i].can_id, 4);
    DebugSerial.write(canFrames[i].can_dlc);
    DebugSerial.write(canFrames[i].data, canFrames[i].can_dlc);
  }
  LoRa.endPacket(true);
}

void loop()
{
  if (LoRa.isAsyncTxDone() || initialSend)
  {
    initialSend = false;
    LoRa.beginPacket();
    loraSendDummyRadioHeadHeader();
    LoRa.print("hello world");
    LoRa.endPacket(true);
    delay(500);
    }
  return;

  MCP2515::ERROR res = mcp2515.readMessage(&canFrames[packetIndex]);
  if (res == MCP2515::ERROR_OK)
  {
    packetIndex++;
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
  }
  else
  {
    if (res != MCP2515::ERROR_NOMSG)
    {
      DebugSerial.printf("Got CAN res: %d\n", res);
    }
  }
}
