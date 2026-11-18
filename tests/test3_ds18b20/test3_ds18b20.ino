/*
 * TEST 3: DS18B20 TEMPERATURE
 * Serial Monitor (115200) me har second temperature dikhega.
 * Room me ~25-30 C, probe mutthi me pakdo to badhega.
 * Libraries: OneWire, DallasTemperature
 */
#include <OneWire.h>
#include <DallasTemperature.h>

#define TEMP_PIN 4   // probe ka Yellow (DATA) taar, 4.7k resistor se 3V3 pe bhi

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

void setup() {
  Serial.begin(115200);
  tempSensor.begin();
  Serial.print("Sensors mile: ");
  Serial.println(tempSensor.getDeviceCount());   // 1 aana chahiye
}

void loop() {
  tempSensor.requestTemperatures();          // reading lo
  float t = tempSensor.getTempCByIndex(0);   // pehle sensor ki value

  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor nahi mila! (-127) Wiring check karo.");
  } else {
    Serial.print("Temperature: ");
    Serial.print(t, 1);
    Serial.println(" C");
  }
  delay(1000);
}
