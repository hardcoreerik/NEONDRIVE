#pragma once

#include <Arduino.h>

struct GpsFix {
  bool    valid       = false;
  double  lat         = 0.0;
  double  lon         = 0.0;
  float   speedKnots  = 0.0f;
  float   altMeters   = 0.0f;
  uint8_t satellites  = 0;
  uint8_t fixQuality  = 0;   // 0=none, 1=GPS, 2=DGPS
  uint32_t lastFixMs  = 0;
  char    timeUtc[10] = {};  // "HHMMSSss"
  char    dateUtc[7]  = {};  // "DDMMYY"
};

namespace GpsService {

// Call once in setup(). rxPin/txPin default to CYD35 GPS pins.
void begin(int rxPin = 16, int txPin = 17, uint32_t baud = 9600);

// Call every loop() tick — feeds NMEA bytes into parser.
void tick();

// Returns the most recent fix (valid flag indicates GPS lock).
const GpsFix& fix();

// True if GPS serial is initialised.
bool isRunning();

// Speed helpers
float speedKmh();
float speedMph();

} // namespace GpsService
