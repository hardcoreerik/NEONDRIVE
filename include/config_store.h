#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "app_config.h"

void printConfigSerial(const AppConfig& c);
void applyDefaults(AppConfig& c);
bool loadConfig(AppConfig& out);
bool saveConfig(const AppConfig& in);

#endif // CONFIG_STORE_H
