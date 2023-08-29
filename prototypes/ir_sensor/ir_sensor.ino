
/*
* This project creates a small thermal camera using the MELEXIS MLX90621
*
* Adapted by Josh Long (https://github.com/longjos) Oct 2015
* Based on a https://github.com/robinvanemden/MLX90621_Arduino_Processing
*
* Original work by:
* 2013 by Felix Bonowski
* Based on a forum post by maxbot: http://forum.arduino.cc/index.php/topic,126244.msg949212.html#msg949212
* This code is in the public domain.
*/
#include <SPI.h>
#include <Arduino.h>
#include <Wire.h>
#include "MLX90621.h"

MLX90621 sensor; // create an instance of the Sensor class

void setup(){
   sensor.initialise(8);
  Serial.begin(115200);
  Serial.println("hello");
 }
void loop(){
  Serial.println("\n");
  sensor.measure(true); //get new readings from the sensor

  for(int y=0;y<4;y++){ //go through all the rows
    for(int x=0;x<16;x++){ //go through all the columns
      float t = sensor.getTemperature(y+(x*4)); // extract the temperature at position x/y
      Serial.print(t);
      Serial.print(",");
    }
  }
  delay(25);
};

float getScaleValue(float temp, float minTemp, float maxTemp){
  uint8_t scale = (31.0 / (maxTemp - minTemp))*(temp) + (-1.0) * minTemp * (31.0)/(maxTemp - minTemp);
  uint16_t color = scale << 11 | scale << 6 | 1 << 5 | scale;
  return color;
}
