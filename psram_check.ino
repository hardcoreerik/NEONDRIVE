#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  if (psramFound()) {
    Serial.println("PSRAM detected!");
    Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("No PSRAM found.");
  }
}

void loop() {}