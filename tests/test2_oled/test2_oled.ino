/*
 * TEST 2: OLED
 * OLED pe "Hello" aur ek counter dikhna chahiye.
 * Library: Adafruit SSD1306 (+ Adafruit GFX)
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
int counter = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // 0x3C = OLED ka address (kuch boards pe 0x3D hota hai)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED nahi mila!");
    while (1);
  }
  Serial.println("OLED OK");
}

void loop() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("Hello!");

  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print("Counter: ");
  display.print(counter++);

  display.display();
  delay(1000);
}
