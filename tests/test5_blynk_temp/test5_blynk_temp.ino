/*
 * TEST 5: BLYNK + TEMPERATURE (jaldi wala Blynk test)
 * Sirf DS18B20 probe chahiye (test 3 wali wiring). Temperature har 2 sec
 * Blynk app me V0 pe aayega.
 *
 * Upload se pehle:
 *   1. Neeche ki 3 Blynk lines apne Blynk device se copy karke daalo
 *   2. WiFi naam/password daalo (2.4 GHz WiFi ya phone hotspot)
 *   3. Blynk template me V0 datastream (Double) bana hona chahiye
 *
 * Serial Monitor (115200):
 *   "Ready (ping ...)"  -> Blynk se jud gaya
 *   "Temp bheja: 27.5 C" har 2 sec
 * Libraries: Blynk, OneWire, DallasTemperature
 */

// ----- Blynk device se copy karo (sabse upar hi rehni chahiye) -----
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Health Monitor"
#define BLYNK_AUTH_TOKEN    "YourAuthTokenHere"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ----- Apna WiFi (sirf is test ke liye, v2 me phone se set hota hai) -----
char ssid[] = "YourWiFiName";
char pass[] = "YourWiFiPassword";

#define TEMP_PIN 4   // probe ka Yellow (DATA), 4.7k resistor se 3V3 pe bhi

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
BlynkTimer timer;

void sendTemp() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);

  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor nahi mila! (-127) Wiring check karo.");
    return;
  }
  Blynk.virtualWrite(V0, t);
  Serial.print("Temp bheja: ");
  Serial.print(t, 1);
  Serial.println(" C");
}

void setup() {
  Serial.begin(115200);
  tempSensor.begin();

  // WiFi + Blynk se judo (judne tak yahin rukega, test hai to chalega)
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(2000L, sendTemp);
}

void loop() {
  Blynk.run();
  timer.run();
}
