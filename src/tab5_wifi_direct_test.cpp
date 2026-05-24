#if defined(TAB5_WIFI_DIRECT_TEST)
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

// Official Tab5 SDIO mapping from M5 docs:
// CLK=12 CMD=13 D0=11 D1=10 D2=9 D3=8 RST=15
//
// Note: In this PlatformIO core, WiFi.setPins(...) is not exposed on WiFiClass.
// We therefore rely on compile-time hosted SDIO pin override from:
// include/tab5_sdkconfig_override.h

static uint32_t s_lastScanMs = 0;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(300);

  Serial.println("[direct] Tab5 WiFi direct test boot");
  Serial.println("[direct] expected SDIO pins: clk=12 cmd=13 d0=11 d1=10 d2=9 d3=8 rst=15");

  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("Tab5 WiFi Direct Test");

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("[direct] WiFi.mode(WIFI_STA) failed");
    M5.Display.println("WiFi.mode failed");
    return;
  }

  Serial.println("[direct] WiFi.mode(WIFI_STA) ok");
  M5.Display.println("WiFi STA mode OK");
  s_lastScanMs = millis() - 3000;  // immediate first scan
}

void loop() {
  M5.update();
  if (millis() - s_lastScanMs < 5000) return;
  s_lastScanMs = millis();

  Serial.println("[direct] scan start");
  M5.Display.fillRect(0, 80, M5.Display.width(), M5.Display.height() - 80, BLACK);
  M5.Display.setCursor(8, 88);
  M5.Display.println("Scan start...");

  int n = WiFi.scanNetworks(false, true);
  Serial.printf("[direct] scan done n=%d\n", n);
  M5.Display.printf("Scan done: %d\n", n);

  if (n > 0) {
    int maxPrint = (n > 8) ? 8 : n;
    for (int i = 0; i < maxPrint; ++i) {
      Serial.printf("[direct] ap[%d] ssid=%s rssi=%d ch=%d\n",
                    i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
      M5.Display.printf("%d:%s (%d)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      delay(10);
    }
  }

  WiFi.scanDelete();
}
#endif
