#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(33, 36);   // CS=33, IRQ=36 (standard on most S024 boards)

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nPORKCHOP CYD TEST STARTING...");

  // Display
  tft.init();
  tft.setRotation(1);        // Landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("PORKCHOP LIVES", 10, 60);
  tft.setTextSize(2);
  tft.drawString("CYD Port test v0.1", 20, 120);

  // Touch
  ts.begin();
  ts.setRotation(1);

  // Backlight - try 21 first, then 27 if screen stays black
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  // pinMode(27, OUTPUT); digitalWrite(27, HIGH);  // uncomment if needed
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    tft.fillCircle(p.x, p.y, 8, TFT_RED);
    Serial.printf("Touch X:%d Y:%d\n", p.x, p.y);
  }
  delay(10);
}
