/*
 * ============================================================
 *   ESP32 HEALTH MONITOR  -  v2
 *   Body Temp (DS18B20) + Heart Rate & SpO2 (MAX30102)
 *   Display: 0.96" OLED  |  Mobile: Blynk IoT app
 * ============================================================
 *
 *  v2 me NAYA: WiFi naam/password code me nahi likhna padta.
 *    - Board ka BOOT button 3 second dabake rakho -> ESP32 apna hotspot
 *      banata hai (sirf tabhi, apne aap kabhi nahi khulta):
 *        WiFi naam : HealthMon-Setup
 *        Password  : health123
 *    - Phone se us hotspot se judo -> setup page khulega
 *      (na khule to browser me 192.168.4.1 kholo)
 *    - Apna WiFi chuno, password daalo, Save -> ESP32 yaad rakhega
 *    - WiFi badalna ho: wahi, BOOT button 3 second
 *    - 3 min tak kuch save nahi kiya to hotspot band
 *    - Board ki LED (GPIO 2) jalti hai = WiFi juda hai
 *    Setup ke time bhi OLED pe readings chalti rehti hain.
 *
 *  WIRING: diagrams/step5_final_v2.png. BOOT button aur blue LED
 *  ESP32 board pe pehle se hain, kuch naya jodna nahi.
 *
 *  LIBRARIES (Arduino IDE > Library Manager se install karo):
 *    1. Blynk
 *    2. SparkFun MAX3010x Pulse and Proximity Sensor Library
 *    3. OneWire
 *    4. DallasTemperature
 *    5. Adafruit SSD1306   (Adafruit GFX saath me install ho jayegi)
 *    6. WiFiManager  (by tzapu)   <-- v2 me naya
 *
 *  BLYNK DATASTREAMS (Virtual pins):
 *    V0 = Body Temp (°C)   V1 = Heart Rate (BPM)
 *    V2 = SpO2 (%)         V3 = Status (String)
 *
 *  BLYNK EVENTS (Template > Events me ye 3 codes banao):
 *    high_temp, low_spo2, abnormal_bpm
 *
 *  NOTE: Ye hobby project hai, medical device nahi.
 */

// ---------------- 1. BLYNK DETAILS (apne Blynk device se copy karo) ----------------
// Ye 3 lines sabse upar honi chahiye, #include se pehle
#define BLYNK_TEMPLATE_ID   "TMPL3xti-Jf-f"
#define BLYNK_TEMPLATE_NAME "Health Monitor"
#define BLYNK_AUTH_TOKEN    "G_UvAgGBpsfydKyLu46PfAKDNuIYES6I"

#define BLYNK_PRINT Serial   // Blynk ke messages Serial Monitor pe dikhenge

// ---------------- 2. WIFI SETUP HOTSPOT ----------------
// WiFi naam/password ab yahan nahi, phone se setup page pe daalna hai
#define AP_NAME  "HealthMon-Setup"   // ESP32 ka setup hotspot
#define AP_PASS  "health123"         // hotspot password (kam se kam 8 letters)
#define PORTAL_TIMEOUT 180           // setup hotspot 3 min baad band

// ---------------- 3. LIBRARIES ----------------
#include <WiFi.h>
#include <WiFiManager.h>       // phone se WiFi setup
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include "MAX30105.h"          // MAX30102 bhi isi library se chalta hai
#include "spo2_algorithm.h"    // SpO2 + heart rate nikalne wala algorithm
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- 4. PINS ----------------
#define TEMP_PIN    4     // DS18B20 data pin (4.7k resistor isse 3V3 tak zaroori)
#define BUZZER_PIN  25    // Buzzer (optional)
#define BOOT_BTN    0     // board ka BOOT button (WiFi setup ke liye)
#define WIFI_LED    2     // board ki blue LED: jale = WiFi juda hai

// ---------------- 5. ALERT LIMITS ----------------
#define HIGH_TEMP_LIMIT  38.0   // isse upar = bukhar
#define LOW_SPO2_LIMIT   92     // isse neeche = oxygen kam
#define HIGH_BPM_LIMIT   120    // isse upar = dhadkan tez
#define LOW_BPM_LIMIT    50     // isse neeche = dhadkan dheemi

// ---------------- 6. OBJECTS ----------------
MAX30105 pulseSensor;                         // pulse oximeter
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);       // temperature sensor
Adafruit_SSD1306 display(128, 64, &Wire, -1); // OLED 128x64
BlynkTimer timer;                             // delay() ki jagah timer
WiFiManager wm;                               // WiFi setup page

// ---------------- 7. VARIABLES ----------------
// MAX30102 ke samples yahan store honge (100 samples = 4 second ka data)
#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int sampleCount = 0;

// Final readings
// (volatile: pulse wala alag task inhe likhta hai, loop() padhta hai)
float bodyTemp = 0;
volatile int heartRate = 0;
volatile int spo2      = 0;
volatile bool fingerOn = false;
bool tempOk    = false;   // DS18B20 se sahi reading mili ya nahi
bool pulseOk   = false;   // MAX30102 mila ya nahi

// OLED aur MAX30102 dono same I2C wire pe hain, ek time pe ek hi use kare
SemaphoreHandle_t i2cLock;

// Ek hi alert baar baar na aaye: same alert dobara 1 minute baad hi jayega
#define ALERT_GAP 60000
unsigned long lastTempAlert = 0;
unsigned long lastSpo2Alert = 0;
unsigned long lastBpmAlert  = 0;

// BOOT button kab se daba hai (0 = daba nahi hai)
unsigned long btnPressStart = 0;

// WiFi / Blynk dobara try karne ka hisaab
unsigned long lastWiFiTry   = 0;
unsigned long lastBlynkTry  = 0;


// ============================================================
//   SETUP: ek baar chalta hai
// ============================================================
void setup() {
  Serial.begin(115200);
  i2cLock = xSemaphoreCreateMutex();   // OLED use karne se pehle banana zaroori
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BOOT_BTN, INPUT_PULLUP);   // dabane pe LOW
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, LOW);

  Wire.begin(21, 22);   // I2C: SDA=21, SCL=22

  // ----- OLED start -----
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED nahi mila! Wiring check karo.");
  }
  showMessage("Starting...");

  // ----- MAX30102 start -----
  // Sensor na mile to bhi baaki project (temp, OLED, Blynk) chalta rahega
  pulseOk = pulseSensor.begin(Wire, I2C_SPEED_FAST);
  if (pulseOk) {
    // Settings: brightness=60, average=4, mode=2 (Red+IR), rate=100, pulseWidth=411, range=4096
    // (100 samples/sec ÷ average 4 = 25 samples/sec, jo algorithm ko chahiye)
    pulseSensor.setup(60, 4, 2, 100, 411, 4096);

    // Pulse sensor ko alag task me padho. Library sirf 4 samples (160 ms)
    // yaad rakhti hai; WiFi connect / setup page / Blynk reconnect me loop()
    // 2-10 sec atak sakta hai, tab samples kho jaate aur BPM/SpO2 galat aata.
    // Ye task loop() se upar priority pe hai, isliye kabhi nahi rukta.
    xTaskCreate(pulseTask, "pulse", 4096, NULL, 2, NULL);
  } else {
    Serial.println("MAX30102 nahi mila! Wiring check karo.");
    showMessage("MAX30102 Error!");
    delay(2000);   // setup me hai, isliye yahan delay chalega
  }

  // ----- DS18B20 start -----
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);  // reading ka wait mat karo (code ruke nahi)
  tempSensor.requestTemperatures();        // pehli reading shuru

  // ----- WiFi (saved WiFi se judo, warna setup hotspot) -----
  showMessage("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);   // setup page khula ho tab bhi code chalta rahe
  wm.setConnectTimeout(10);            // naya WiFi save karne pe 10 sec try

  // Saved WiFi se background me judo, code rukega nahi.
  // Kuch saved nahi hai to OLED pe "Hold BOOT 3s" dikhega.
  if (wm.getWiFiIsSaved()) {
    WiFi.begin();
    lastWiFiTry = millis();
  }

  Blynk.config(BLYNK_AUTH_TOKEN);

  // Har 2 second me sendData() chalega
  timer.setInterval(2000L, sendData);

  // Har 5 second me WiFi check (WiFi baad me aaye to bhi connect ho jaye)
  timer.setInterval(5000L, checkWiFi);

  Serial.println("Setup complete!");
}


// ============================================================
//   LOOP: baar baar chalta hai (yahan delay() NAHI lagana)
// ============================================================
void loop() {
  wm.process();   // setup page khula ho to use chalao

  // Board LED: WiFi juda hai to ON, warna OFF
  digitalWrite(WIFI_LED, WiFi.status() == WL_CONNECTED ? HIGH : LOW);

  // WiFi connected ho tabhi Blynk chalao, warna OLED offline bhi chalta rahe.
  // Blynk juda nahi hai (internet band / galat token) to har 30 sec me sirf
  // ek koshish: har koshish 3-4 sec atakti hai, Blynk khud har 5 sec karta
  // to OLED aur button baar baar hang hote.
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected() || millis() - lastBlynkTry < 4000) {
      Blynk.run();
    } else if (lastBlynkTry == 0 || millis() - lastBlynkTry > 30000) {
      lastBlynkTry = millis();   // agle 4 sec Blynk.run() -> ek connect koshish
    }
  }

  timer.run();      // 2 sec wala timer
  checkButton();    // BOOT button 3 sec = WiFi setup
}


// ============================================================
//   MAX30102 wala task (loop() se alag, lagatar chalta hai)
// ============================================================
void pulseTask(void *param) {
  for (;;) {
    xSemaphoreTake(i2cLock, portMAX_DELAY);
    readPulse();
    xSemaphoreGive(i2cLock);
    vTaskDelay(pdMS_TO_TICKS(20));   // 25 samples/sec aate hain, 20 ms kaafi hai
  }
}


// ============================================================
//   WiFi nahi hai to dobara try karo (code rukta nahi)
// ============================================================
void checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (wm.getConfigPortalActive()) return;   // setup chal raha hai, use chhed mat
  if (!wm.getWiFiIsSaved()) return;         // koi WiFi saved hi nahi, BOOT ka wait

  // Har 30 sec me saved WiFi se dobara try
  // (hotspot band hone ke baad pehli baar turant)
  if (millis() - lastWiFiTry > 30000) {
    Serial.println("WiFi nahi hai, dobara try kar rahe hain... (OLED chalta rahega)");
    lastWiFiTry = millis();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin();   // saved WiFi naam/password se
  }
}

// Setup hotspot on (sirf BOOT button se)
void startSetupPortal() {
  Serial.println("WiFi setup hotspot ON: " AP_NAME "  (password: " AP_PASS ")");
  // Purana WiFi tab tak saved rahega jab tak naya save na karo
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT);
  wm.startConfigPortal(AP_NAME, AP_PASS);
  lastWiFiTry = 0;   // hotspot band hote hi saved WiFi turant try ho
  showSetupInfo();
}


// ============================================================
//   BOOT button 3 sec dabao -> WiFi setup hotspot on
// ============================================================
void checkButton() {
  if (digitalRead(BOOT_BTN) == HIGH) {   // button chhoda hua hai
    btnPressStart = 0;
    return;
  }

  if (btnPressStart == 0) btnPressStart = millis();

  if (millis() - btnPressStart > 3000 && !wm.getConfigPortalActive()) {
    startSetupPortal();
  }
}


// ============================================================
//   MAX30102 se Heart Rate + SpO2 padhna
// ============================================================
void readPulse() {
  if (!pulseOk) return;  // sensor hi nahi mila to kuch mat karo

  pulseSensor.check();   // sensor se naye samples lao

  while (pulseSensor.available()) {
    uint32_t red = pulseSensor.getFIFORed();
    uint32_t ir  = pulseSensor.getFIFOIR();
    pulseSensor.nextSample();

    // IR value kam = ungli sensor pe nahi hai
    if (ir < 50000) {
      fingerOn = false;
      heartRate = 0;
      spo2 = 0;
      sampleCount = 0;   // buffer reset, ungli rakhne pe fresh start
      continue;
    }
    fingerOn = true;

    // Sample buffer me daalo
    redBuffer[sampleCount] = red;
    irBuffer[sampleCount]  = ir;
    sampleCount++;

    // Buffer full (100 samples = 4 sec) -> calculate karo
    if (sampleCount == BUFFER_SIZE) {
      calculateHeartRateAndSpO2();

      // Purane 25 samples hatao, baaki 75 aage shift karo.
      // Ab har 1 sec (25 naye samples) me nayi reading aayegi.
      for (int i = 25; i < BUFFER_SIZE; i++) {
        redBuffer[i - 25] = redBuffer[i];
        irBuffer[i - 25]  = irBuffer[i];
      }
      sampleCount = 75;
    }
  }
}

void calculateHeartRateAndSpO2() {
  int32_t newSpO2, newHR;
  int8_t spo2Valid, hrValid;

  // SparkFun/Maxim ka ready-made algorithm
  maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_SIZE, redBuffer,
                                         &newSpO2, &spo2Valid, &newHR, &hrValid);

  // Sirf sahi range wali readings lo (galat/noise wali ignore)
  if (hrValid && newHR > 40 && newHR < 180) {
    heartRate = newHR;
  }
  if (spo2Valid && newSpO2 > 80 && newSpO2 <= 100) {
    spo2 = newSpO2;
  }
}


// ============================================================
//   DS18B20 se Body Temperature padhna
// ============================================================
void readTemperature() {
  float t = tempSensor.getTempCByIndex(0);   // pichli request ki reading

  // -127 = sensor connected nahi  ->  purani value mat dikhao
  // 85 = power on ke baad ki default value (asli reading nahi) -> ignore
  if (t == DEVICE_DISCONNECTED_C) {
    tempOk = false;
  } else if (t != 85.0) {
    bodyTemp = t;
    tempOk = true;
  }

  tempSensor.requestTemperatures();   // agli reading ke liye request (2 sec me ready)
}


// ============================================================
//   Har 2 sec: readings -> Blynk + OLED + alerts
// ============================================================
void sendData() {
  readTemperature();

  // Serial Monitor pe print (debugging ke liye)
  Serial.print("Temp: ");
  if (tempOk) Serial.print(bodyTemp, 1); else Serial.print("--");
  Serial.print(" C | BPM: "); Serial.print(heartRate);
  Serial.print(" | SpO2: ");  Serial.print(spo2);
  Serial.println(fingerOn ? " %" : " % (no finger)");

  // Status message (app me dikhega)
  String status;
  if (!pulseOk)       status = "Pulse sensor error";
  else if (!tempOk)   status = "Temp sensor error";
  else if (!fingerOn) status = "Place finger";
  else                status = "Measuring";

  // ----- Blynk app ko data bhejo -----
  if (Blynk.connected()) {
    if (tempOk) Blynk.virtualWrite(V0, bodyTemp);   // galat/purani temp mat bhejo
    Blynk.virtualWrite(V1, heartRate);
    Blynk.virtualWrite(V2, spo2);
    Blynk.virtualWrite(V3, status);
  }

  checkAlerts();
  updateOLED();
}


// ============================================================
//   Alerts: buzzer + phone notification
// ============================================================
void checkAlerts() {
  bool highTemp = (tempOk && bodyTemp > HIGH_TEMP_LIMIT);
  bool lowSpo2  = (fingerOn && spo2 > 0 && spo2 < LOW_SPO2_LIMIT);
  bool badBpm   = (fingerOn && heartRate > 0 &&
                   (heartRate > HIGH_BPM_LIMIT || heartRate < LOW_BPM_LIMIT));

  // Koi bhi problem ho to buzzer ON
  digitalWrite(BUZZER_PIN, (highTemp || lowSpo2 || badBpm) ? HIGH : LOW);

  // Phone notification (Blynk connected ho tabhi, aur same alert 1 min me ek hi baar)
  if (highTemp && canSendAlert(lastTempAlert)) {
    Blynk.logEvent("high_temp", String("High temperature: ") + bodyTemp + " C");
  }
  if (lowSpo2 && canSendAlert(lastSpo2Alert)) {
    Blynk.logEvent("low_spo2", String("Low SpO2: ") + spo2 + " %");
  }
  if (badBpm && canSendAlert(lastBpmAlert)) {
    Blynk.logEvent("abnormal_bpm", String("Abnormal heart rate: ") + heartRate + " BPM");
  }
}

// Alert bhej sakte hain ya nahi? (offline ho ya 1 min nahi hua to NAHI)
bool canSendAlert(unsigned long &lastTime) {
  if (!Blynk.connected()) return false;   // offline: baad me online hone pe bhejenge
  if (lastTime != 0 && millis() - lastTime < ALERT_GAP) return false;
  lastTime = millis();
  return true;
}


// ============================================================
//   OLED pe readings dikhana
// ============================================================
void updateOLED() {
  // Setup hotspot on ho to har dusri baar (2 sec) setup ki info dikhao,
  // warna readings use turant mita deti aur password padh hi nahi paate
  static bool infoTurn = false;
  if (wm.getConfigPortalActive()) {
    infoTurn = !infoTurn;
    if (infoTurn) {
      showSetupInfo();
      return;
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Upar wali line: title + WiFi status
  // Setup hotspot on ho to title ki jagah hotspot ka naam dikhao
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (wm.getConfigPortalActive()) {
    display.print(AP_NAME);
    display.setCursor(104, 0);
    display.print("AP");                              // AP = setup hotspot on
  } else if (WiFi.status() != WL_CONNECTED) {
    display.print("Hold BOOT 3s: WiFi");              // WiFi nahi -> setup kaise karein
  } else {
    display.print("Health Monitor");
    display.setCursor(104, 0);
    display.print(Blynk.connected() ? "ON" : "--");   // ON = Blynk se juda hai
  }

  // Temperature (bade font me)
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print("T:");
  if (tempOk) { display.print(bodyTemp, 1); display.print("C"); }
  else          display.print("--");          // sensor nahi mila

  if (!pulseOk) {
    // MAX30102 nahi mila
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Pulse sensor error");
    display.setCursor(0, 52);
    display.print("Check wiring");
  } else if (fingerOn) {
    // Heart rate + SpO2
    display.setCursor(0, 32);
    display.print("BPM:");
    if (heartRate > 0) display.print(heartRate);
    else               display.print("..");    // abhi calculate ho raha hai

    display.setCursor(0, 50);
    display.print("SpO2:");
    if (spo2 > 0) { display.print(spo2); display.print("%"); }
    else            display.print("..");
  } else {
    // Ungli nahi hai
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Place finger on");
    display.setCursor(0, 52);
    display.print("pulse sensor");
  }

  oledShow();
}

// Chhota helper: OLED pe ek message dikhao
void showMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 28);
  display.print(msg);
  oledShow();
}

// WiFi setup ke steps OLED pe
void showSetupInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("WiFi SETUP");
  display.setCursor(0, 16);
  display.print("Phone WiFi: ");
  display.setCursor(0, 26);
  display.print(AP_NAME);
  display.setCursor(0, 38);
  display.print("Pass: " AP_PASS);
  display.setCursor(0, 52);
  display.print("Open 192.168.4.1");
  oledShow();
}

// OLED buffer screen pe bhejo (I2C lock leke, pulse task ke saath takkar na ho)
void oledShow() {
  xSemaphoreTake(i2cLock, portMAX_DELAY);
  display.display();
  xSemaphoreGive(i2cLock);
}
