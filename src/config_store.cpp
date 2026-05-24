#include "config_store.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

void printConfigSerial(const AppConfig& c) {
  Serial.println("---- config ----");
  Serial.print("version="); Serial.println(c.version);
  Serial.print("wifi.channelHopInterval="); Serial.println(c.wifi_channelHopInterval);
  Serial.print("wifi.scanDuration="); Serial.println(c.wifi_scanDuration);
  Serial.print("wifi.enableDeauth="); Serial.println(c.wifi_enableDeauth ? "true" : "false");
  Serial.print("wifi.ssid="); Serial.println(c.wifi_ssid.c_str());
  Serial.print("wifi.password_set="); Serial.println(c.wifi_password.isEmpty() ? "false" : "true");
  Serial.print("wifi.saved_count="); Serial.println(c.wifi_savedCount);
  for (int i = 0; i < c.wifi_savedCount; i++) {
    Serial.printf("wifi.saved[%d].ssid=%s password_set=%s\n",
                  i,
                  c.wifi_savedSsid[i].c_str(),
                  c.wifi_savedPassword[i].isEmpty() ? "false" : "true");
  }
  Serial.print("wpasec.apikey_set=");   Serial.println(c.wpasec_apikey.isEmpty()  ? "false" : "true");
  Serial.print("wigle.apiname_set=");   Serial.println(c.wigle_apiname.isEmpty()  ? "false" : "true");
  Serial.print("wigle.apitoken_set=");  Serial.println(c.wigle_apitoken.isEmpty() ? "false" : "true");
  Serial.print("dropbox.token_set=");   Serial.println(c.dropbox_token.isEmpty()  ? "false" : "true");
  Serial.print("webhook.url_set=");     Serial.println(c.webhook_url.isEmpty()    ? "false" : "true");
  Serial.print("ntfy.topic_set=");      Serial.println(c.ntfy_topic.isEmpty()     ? "false" : "true");
  Serial.print("mqtt.broker_set=");     Serial.println(c.mqtt_broker.isEmpty()    ? "false" : "true");
  Serial.print("display.showStats="); Serial.println(c.display_showStats ? "true" : "false");
  Serial.print("display.timeout="); Serial.println(c.display_timeout);
  Serial.print("startup.autoReconnectPrompt="); Serial.println(c.startup_autoReconnectPrompt ? "true" : "false");
  Serial.print("startup.autoRotate="); Serial.println(c.startup_autoRotate ? "true" : "false");
  Serial.print("startup.manualRotation="); Serial.println(c.startup_manualRotation);
  Serial.print("wifi.defaultLockChannel="); Serial.println(c.wifi_defaultLockChannel ? "true" : "false");
  Serial.print("telemetry.monitorIntervalMs="); Serial.println(c.telemetry_monitorIntervalMs);
  Serial.print("telemetry.verboseSerial="); Serial.println(c.telemetry_verboseSerial ? "true" : "false");
  Serial.print("display.brightness="); Serial.println(c.display_brightness);
  Serial.println("----------------");
}

void applyDefaults(AppConfig& c) {
  c = AppConfig{};
}

bool loadConfig(AppConfig& out) {
  if (!LittleFS.exists(CONFIG_PATH)) {
    Serial.println("[cfg] config.json not found; will create defaults.");
    return false;
  }

  fs::File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Serial.println("[cfg] failed to open config.json for read; will create defaults.");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("[cfg] JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  out.version = String(doc["version"] | "1.0.0");
  JsonObject wifi = doc["wifi"];
  out.wifi_channelHopInterval = wifi["channelHopInterval"] | 500;
  out.wifi_scanDuration       = wifi["scanDuration"]       | 2000;
  out.wifi_enableDeauth       = wifi["enableDeauth"]       | true;
  out.wifi_ssid               = String(wifi["ssid"] | "");
  out.wifi_password           = String(wifi["password"] | "");
  out.wifi_savedCount         = 0;
  JsonArray saved = wifi["saved"].as<JsonArray>();
  if (!saved.isNull()) {
    for (JsonVariant v : saved) {
      if (out.wifi_savedCount >= AppConfig::MAX_SAVED_WIFI) break;
      JsonObject e = v.as<JsonObject>();
      String ssid = String(e["ssid"] | "");
      if (ssid.isEmpty()) continue;
      out.wifi_savedSsid[out.wifi_savedCount] = ssid;
      out.wifi_savedPassword[out.wifi_savedCount] = String(e["password"] | "");
      out.wifi_savedCount++;
    }
  }
  if (out.wifi_savedCount == 0 && !out.wifi_ssid.isEmpty()) {
    out.wifi_savedSsid[0] = out.wifi_ssid;
    out.wifi_savedPassword[0] = out.wifi_password;
    out.wifi_savedCount = 1;
  }
  if (out.wifi_ssid.isEmpty() && out.wifi_savedCount > 0) {
    out.wifi_ssid = out.wifi_savedSsid[0];
    out.wifi_password = out.wifi_savedPassword[0];
  }

  JsonObject wpasec = doc["wpasec"];
  out.wpasec_apikey = String(wpasec["apikey"] | "");

  JsonObject wigle = doc["wigle"];
  out.wigle_apiname  = String(wigle["apiname"]  | "");
  out.wigle_apitoken = String(wigle["apitoken"] | "");

  out.dropbox_token  = String(doc["dropbox"]["token"]  | "");
  out.dropbox_folder = String(doc["dropbox"]["folder"] | "/WardriveAnalyzerSync");
  out.webhook_url   = String(doc["webhook"]["url"]   | "");
  out.ntfy_topic    = String(doc["ntfy"]["topic"]    | "");

  JsonObject mqtt = doc["mqtt"];
  out.mqtt_broker       = String(mqtt["broker"]      | "");
  out.mqtt_port         = mqtt["port"]               | 1883;
  out.mqtt_topic_prefix = String(mqtt["topicPrefix"] | "neondrive");
  out.mqtt_username     = String(mqtt["username"]    | "");
  out.mqtt_password     = String(mqtt["password"]    | "");

  JsonObject display = doc["display"];
  out.display_brightness = display["brightness"] | 128;
  out.display_timeout    = display["timeout"]    | 30;
  out.display_showStats  = display["showStats"]  | true;
  out.ui_hypercube       = display["hypercube"]  | true;

  JsonObject startup = doc["startup"];
  out.startup_autoReconnectPrompt = startup["autoReconnectPrompt"] | true;
  out.startup_autoRotate          = startup["autoRotate"]          | true;
  out.startup_manualRotation      = startup["manualRotation"]      | 1;
  if (out.startup_manualRotation < 0) out.startup_manualRotation = 0;
  if (out.startup_manualRotation > 3) out.startup_manualRotation = 3;
  out.startup_webserver           = startup["webserver"]           | false;
  out.wifi_defaultLockChannel = wifi["defaultLockChannel"] | false;

  JsonObject telemetry = doc["telemetry"];
  out.telemetry_monitorIntervalMs = telemetry["monitorIntervalMs"] | 500;
  if (out.telemetry_monitorIntervalMs < 200) out.telemetry_monitorIntervalMs = 200;
  if (out.telemetry_monitorIntervalMs > 2000) out.telemetry_monitorIntervalMs = 2000;
  out.telemetry_verboseSerial = telemetry["verboseSerial"] | false;

  return true;
}

bool saveConfig(const AppConfig& in) {
  JsonDocument doc;
  doc["version"] = in.version;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["channelHopInterval"] = in.wifi_channelHopInterval;
  wifi["scanDuration"]       = in.wifi_scanDuration;
  wifi["enableDeauth"]       = in.wifi_enableDeauth;
  wifi["ssid"]               = in.wifi_ssid.c_str();
  wifi["password"]           = in.wifi_password.c_str();
  JsonArray saved = wifi["saved"].to<JsonArray>();
  for (int i = 0; i < in.wifi_savedCount && i < AppConfig::MAX_SAVED_WIFI; i++) {
    if (in.wifi_savedSsid[i].isEmpty()) continue;
    JsonObject e = saved.add<JsonObject>();
    e["ssid"] = in.wifi_savedSsid[i].c_str();
    e["password"] = in.wifi_savedPassword[i].c_str();
  }

  JsonObject wpasec = doc["wpasec"].to<JsonObject>();
  wpasec["apikey"] = in.wpasec_apikey.c_str();

  JsonObject wigle = doc["wigle"].to<JsonObject>();
  wigle["apiname"]  = in.wigle_apiname.c_str();
  wigle["apitoken"] = in.wigle_apitoken.c_str();

  doc["dropbox"]["token"]  = in.dropbox_token.c_str();
  doc["dropbox"]["folder"] = in.dropbox_folder.c_str();
  doc["webhook"]["url"]   = in.webhook_url.c_str();
  doc["ntfy"]["topic"]    = in.ntfy_topic.c_str();

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["broker"]      = in.mqtt_broker.c_str();
  mqtt["port"]        = in.mqtt_port;
  mqtt["topicPrefix"] = in.mqtt_topic_prefix.c_str();
  mqtt["username"]    = in.mqtt_username.c_str();
  mqtt["password"]    = in.mqtt_password.c_str();

  JsonObject display = doc["display"].to<JsonObject>();
  display["brightness"] = in.display_brightness;
  display["timeout"]    = in.display_timeout;
  display["showStats"]  = in.display_showStats;
  display["hypercube"]  = in.ui_hypercube;

  JsonObject startup = doc["startup"].to<JsonObject>();
  startup["autoReconnectPrompt"] = in.startup_autoReconnectPrompt;
  startup["autoRotate"]          = in.startup_autoRotate;
  startup["manualRotation"]      = in.startup_manualRotation;
  startup["webserver"]           = in.startup_webserver;
  wifi["defaultLockChannel"] = in.wifi_defaultLockChannel;

  JsonObject telemetry = doc["telemetry"].to<JsonObject>();
  telemetry["monitorIntervalMs"] = in.telemetry_monitorIntervalMs;
  telemetry["verboseSerial"] = in.telemetry_verboseSerial;

  fs::File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    Serial.println("[cfg] failed to open config.json for write.");
    return false;
  }

  if (serializeJsonPretty(doc, f) == 0) {
    Serial.println("[cfg] serializeJsonPretty failed.");
    f.close();
    return false;
  }
  f.close();

  Serial.println("[cfg] saved /config.json");
  return true;
}
