#include <CircularBuffer.h>
#include "Adafruit_VL53L1X.h"

#define IRQ_PIN 2
#define XSHUT_PIN 3
#define BUF_SIZE 25
Adafruit_VL53L1X front_sensor = Adafruit_VL53L1X(XSHUT_PIN, IRQ_PIN);

CircularBuffer<int16_t,BUF_SIZE> s1_buf;

void set_i2c_mux(uint8_t bus)
{
  Wire.beginTransmission(0x70); // TCA9548A address
  Wire.write(1 << bus);         // send byte to select bus
  Wire.endTransmission();
}

void init_sensor(Adafruit_VL53L1X &sensor)
{
  if (!sensor.begin(0x29, &Wire))
  {
    Serial.print(F("Error on init of VL sensor: "));
    Serial.println(sensor.vl_status);
    while (1)
      delay(10);
  }
  Serial.println(F("VL53L1X sensor OK!"));

  if (!sensor.startRanging())
  {
    Serial.print(F("Couldn't start ranging: "));
    Serial.println(sensor.vl_status);
  }

  // Valid timing budgets: 15, 20, 33, 50, 100, 200 and 500ms!
  sensor.setTimingBudget(500);
}

// returns -1 or 0 if it fails
int16_t read_sensor(Adafruit_VL53L1X &sensor)
{
  int16_t distance;
  if (sensor.dataReady())
  {
    distance = sensor.distance();
    sensor.clearInterrupt();
    return distance;
  }
  return 0;
}

double avg(CircularBuffer<int16_t,BUF_SIZE> &cb) {
  if (cb.size() == 0) return 0;
  double total = 0;
  for (int i = 0; i < cb.size(); i++) {
    total += cb[i];
  }
  return total / cb.size();
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    delay(10);
  }

  Wire.begin();
  init_sensor(front_sensor);
}

void loop()
{
  int16_t d1 = read_sensor(front_sensor);
  if (d1 > 0) {
    s1_buf.push(d1);

    double avg_d1 = avg(s1_buf);
    Serial.print(F("d= "));
    Serial.print(avg_d1);
    Serial.println(" mm");
  }
}
