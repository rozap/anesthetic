// https://www.stm32duino.com/viewtopic.php?t=619
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <mcp2515.h>
#include <SoftwareSerial.h>

#define gpsSerial Serial2
#define Debug Serial1
// SoftwareSerial gpsSerial(PB7 /* RX */, PB6 /* TX */);

struct can_frame lngCan;
struct can_frame latCan;
struct can_frame speedCan;
// struct can_frame timeCan;

MCP2515 mcp2515(PA4);
TinyGPSPlus gps;

void setup()
{
  Debug.begin(9600);
  gpsSerial.begin(9600);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_125KBPS);
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
  mcp2515.sendMessage(&latCan);
  mcp2515.sendMessage(&lngCan);
  mcp2515.sendMessage(&speedCan);
}

void loop()
{
  while (gpsSerial.available())
  {
    if (gps.encode(gpsSerial.read()))
    {

      if (gps.location.isUpdated())
      {
        fillPackets();
        sendPackets();
      }
    }
  }
}