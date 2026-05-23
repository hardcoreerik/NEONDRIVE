#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

struct AppConfig {
  static constexpr int MAX_SAVED_WIFI = 8;
  int  wifi_channelHopInterval = 500;
  int  wifi_scanDuration       = 2000;
  bool wifi_enableDeauth       = true;
  String wifi_ssid             = "";
  String wifi_password         = "";
  int  wifi_savedCount         = 0;
  String wifi_savedSsid[MAX_SAVED_WIFI];
  String wifi_savedPassword[MAX_SAVED_WIFI];
  String wpasec_apikey         = "";

  String wigle_apiname         = "";
  String wigle_apitoken        = "";

  String dropbox_token         = "";
  String dropbox_folder        = "/WardriveAnalyzerSync";

  String webhook_url           = "";
  String ntfy_topic            = "";

  String mqtt_broker           = "";
  int    mqtt_port             = 1883;
  String mqtt_topic_prefix     = "neondrive";
  String mqtt_username         = "";
  String mqtt_password         = "";

  bool display_showStats  = true;
  int  display_timeout    = 30;
  bool ui_hypercube       = true;

  bool startup_autoReconnectPrompt = true;
  bool startup_autoRotate          = true;
  bool startup_webserver           = false;
  bool wifi_defaultLockChannel     = false;
  int  telemetry_monitorIntervalMs = 500;
  bool telemetry_verboseSerial     = false;

  int    display_brightness = 128;
  String version            = "1.0.0";
};

#endif // APP_CONFIG_H
