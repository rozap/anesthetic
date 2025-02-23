// https://www.stm32duino.com/viewtopic.php?t=619

#include <SPI.h>
#include <TinyGPSPlus.h>
#include <mcp2515.h>

#define gpsSerial Serial2
#define Debug Serial1
#define DEBUG

struct can_frame lngCan;
struct can_frame latCan;
struct can_frame speedCan;

MCP2515 mcp2515(PA4);
TinyGPSPlus gps;

// wiring
// PA3 is RX, line to GPS TX
// PA2 is TX, line to GPS RX

// mcp2515 same as all the others
void setup()
{
#ifdef DEBUG
  Debug.begin(9600);
#endif
  gpsSerial.begin(9600);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_125KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
}

double lat;
double lng;
double speed;

void fillPackets()
{
  latCan.can_id = 0x0F6;
  latCan.can_dlc = 8;
  lat = gps.location.lat();
  memcpy(latCan.data, &lat, sizeof(double));

  lngCan.can_id = 0x0F7;
  lngCan.can_dlc = 8;
  lng = gps.location.lng();
  memcpy(lngCan.data, &lng, sizeof(double));

  speedCan.can_id = 0x0F8;
  speedCan.can_dlc = 8;
  speed = gps.speed.mph();
  memcpy(speedCan.data, &speed, sizeof(double));
}

void sendPackets()
{
  // fire and forget
  MCP2515::ERROR latErr = mcp2515.sendMessage(MCP2515::TXB0, &latCan);
  MCP2515::ERROR lngErr = mcp2515.sendMessage(MCP2515::TXB1, &lngCan);
  MCP2515::ERROR spdErr = mcp2515.sendMessage(MCP2515::TXB2, &speedCan);

#ifdef DEBUG
  Debug.printf("can send lat=%d lng=%d spd=%d\n", latErr, lngErr, spdErr);
#endif
}

void loop()
{
  while (gpsSerial.available())
  {
    if (gps.encode(gpsSerial.read()))
    {
      if (gps.location.isUpdated())
      {
#ifdef DEBUG
        Debug.printf("Location lat=%f lng=%f\n", gps.location.lat(), gps.location.lng());
#endif
        fillPackets();
        sendPackets();
      }
    }
  }
}
