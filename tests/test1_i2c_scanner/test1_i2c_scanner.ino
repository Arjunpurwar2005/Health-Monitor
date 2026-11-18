/*
 * TEST 1: I2C SCANNER
 * Ye check karta hai ki MAX30102 aur OLED ESP32 se sahi jude hain ya nahi.
 * Serial Monitor (115200) me ye dikhna chahiye:
 *   0x3C -> OLED
 *   0x57 -> MAX30102
 */
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);   // SDA=21, SCL=22
}

void loop() {
  Serial.println("Scanning...");
  int found = 0;

  // Har possible address pe "hello" bolke dekho, jo jawab de wo device hai
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("Device mila: 0x");
      Serial.print(address, HEX);
      if (address == 0x3C) Serial.print("  (OLED)");
      if (address == 0x57) Serial.print("  (MAX30102)");
      Serial.println();
      found++;
    }
  }

  if (found == 0) Serial.println("Koi device nahi mila! Wiring check karo.");
  Serial.println();
  delay(3000);   // test sketch hai, yahan delay chalega
}
