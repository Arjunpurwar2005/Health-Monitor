/*
 * TEST 4: MAX30102 (Heart Rate + SpO2)
 * Ungli halke se sensor pe rakho, hilao mat. ~5 sec baad readings aayengi.
 * Serial Monitor: 115200
 * Library: SparkFun MAX3010x Pulse and Proximity Sensor Library
 */
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 pulseSensor;

#define BUFFER_SIZE 100          // 100 samples = 4 second ka data
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!pulseSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 nahi mila! Wiring check karo.");
    while (1);
  }
  // brightness=60, average=4, Red+IR mode, 100 samples/s, pulseWidth=411, range=4096
  pulseSensor.setup(60, 4, 2, 100, 411, 4096);
  Serial.println("Ungli sensor pe rakho...");
}

void loop() {
  // 100 samples ikatthe karo
  for (int i = 0; i < BUFFER_SIZE; i++) {
    while (!pulseSensor.available()) pulseSensor.check();   // naye sample ka wait
    redBuffer[i] = pulseSensor.getFIFORed();
    irBuffer[i]  = pulseSensor.getFIFOIR();
    pulseSensor.nextSample();
  }

  // Ungli check (IR kam = ungli nahi)
  if (irBuffer[BUFFER_SIZE - 1] < 50000) {
    Serial.println("Ungli nahi hai");
    return;
  }

  int32_t spo2, heartRate;
  int8_t spo2Valid, hrValid;
  maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_SIZE, redBuffer,
                                         &spo2, &spo2Valid, &heartRate, &hrValid);

  Serial.print("BPM: ");
  Serial.print(hrValid ? String(heartRate) : String("--"));
  Serial.print("  |  SpO2: ");
  Serial.print(spo2Valid ? String(spo2) + " %" : String("--"));
  Serial.print("  |  IR: ");
  Serial.println(irBuffer[BUFFER_SIZE - 1]);
}
