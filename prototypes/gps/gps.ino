#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#define RX 0
#define TX 1
SoftwareSerial SoftSerial(RX, TX);

TinyGPSPlus gps;
const char *RADIO_MSG_GPS = "GPS";

char buf[128];

void setup()
{
  SoftSerial.begin(9600);
  Serial.begin(9600);
}

void loop()
{
  while (SoftSerial.available())
  {
    if (gps.encode(SoftSerial.read()))
    {

      displayInfo();
    }
  }
}

char lat[12];
char lng[12];
char speed[8];

void displayInfo()
{
  if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid())
  {

    dtostrf(gps.location.lat(), 11, 6, lat);
    dtostrf(gps.location.lng(), 11, 6, lng);
    dtostrf(gps.speed.mph(), 7, 2, speed);

    sprintf(buf,
            "%s:%s,%s|%04u-%02u-%02uT%02u:%02u:%02u.%02u|%s\n",
            RADIO_MSG_GPS,
            lat,
            lng,
            gps.date.year(),
            gps.date.month(),
            gps.date.day(),
            gps.time.hour(),
            gps.time.minute(),
            gps.time.second(),
            gps.time.centisecond(),
            speed

    );
    Serial.println(buf);
  }
  else
  {
    Serial.println(F("INVALID"));
  }
}
