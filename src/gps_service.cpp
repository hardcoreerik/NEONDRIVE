#include "gps_service.h"

// Minimal NMEA parser — handles $GPRMC / $GNRMC (position, speed, date)
// and $GPGGA / $GNGGA (fix quality, altitude, satellites).
// No external library required.

static HardwareSerial gpsSerial(2);  // Serial2
static bool           gpsRunning = false;
static GpsFix         gpsFix;

// Parse degrees from NMEA ddmm.mmmm format
static double nmeaToDeg(const char* field, char hemi) {
  if (!field || field[0] == '\0') return 0.0;
  double raw = atof(field);
  int deg = (int)(raw / 100);
  double min = raw - deg * 100.0;
  double result = deg + min / 60.0;
  if (hemi == 'S' || hemi == 'W') result = -result;
  return result;
}

// Split a comma-separated NMEA sentence into fields (in-place, modifies buf)
static int splitFields(char* buf, char* fields[], int maxFields) {
  int n = 0;
  fields[n++] = buf;
  for (char* p = buf; *p && n < maxFields; p++) {
    if (*p == ',' || *p == '*') {
      *p = '\0';
      if (n < maxFields) fields[n++] = p + 1;
    }
  }
  return n;
}

static void parseRMC(char* fields[], int n) {
  // $GPRMC,time,status,lat,NS,lon,EW,speed,course,date,...
  if (n < 10) return;
  bool valid = (fields[2][0] == 'A');
  gpsFix.valid = valid;
  if (!valid) return;
  strncpy(gpsFix.timeUtc, fields[1], sizeof(gpsFix.timeUtc) - 1);
  gpsFix.lat        = nmeaToDeg(fields[3], fields[4][0]);
  gpsFix.lon        = nmeaToDeg(fields[5], fields[6][0]);
  gpsFix.speedKnots = atof(fields[7]);
  strncpy(gpsFix.dateUtc, fields[9], sizeof(gpsFix.dateUtc) - 1);
  gpsFix.lastFixMs  = millis();
}

static void parseGGA(char* fields[], int n) {
  // $GPGGA,time,lat,NS,lon,EW,quality,sats,hdop,alt,M,...
  if (n < 10) return;
  gpsFix.fixQuality = atoi(fields[6]);
  gpsFix.satellites = atoi(fields[7]);
  gpsFix.altMeters  = atof(fields[9]);
  if (gpsFix.fixQuality > 0 && !gpsFix.valid) {
    gpsFix.lat = nmeaToDeg(fields[2], fields[3][0]);
    gpsFix.lon = nmeaToDeg(fields[4], fields[5][0]);
    gpsFix.lastFixMs = millis();
  }
}

static char lineBuf[100];
static int  lineLen = 0;

static void processLine() {
  if (lineLen < 6) return;
  // Validate checksum: XOR of bytes between $ and *
  char* star = strrchr(lineBuf, '*');
  if (star && (star - lineBuf) < (int)sizeof(lineBuf) - 2) {
    uint8_t calc = 0;
    for (char* p = lineBuf + 1; p < star; p++) calc ^= (uint8_t)*p;
    uint8_t recv = (uint8_t)strtol(star + 1, nullptr, 16);
    if (calc != recv) return;
    *star = '\0';
  }

  char* fields[20];
  int n = splitFields(lineBuf + 1, fields, 20);  // skip leading '$'
  if (n < 1) return;

  const char* tag = fields[0];
  // Accept both GP and GN talker prefixes
  if (strcmp(tag, "GPRMC") == 0 || strcmp(tag, "GNRMC") == 0) parseRMC(fields, n);
  else if (strcmp(tag, "GPGGA") == 0 || strcmp(tag, "GNGGA") == 0) parseGGA(fields, n);
}

namespace GpsService {

void begin(int rxPin, int txPin, uint32_t baud) {
  gpsSerial.begin(baud, SERIAL_8N1, rxPin, txPin);
  gpsRunning = true;
  lineLen = 0;
  Serial.printf("[gps] UART2 started: RX=%d TX=%d baud=%u\n", rxPin, txPin, baud);
}

void tick() {
  if (!gpsRunning) return;
  while (gpsSerial.available()) {
    char c = (char)gpsSerial.read();
    if (c == '$') {
      lineLen = 0;
    }
    if (lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
      lineBuf[lineLen]   = '\0';
    }
    if (c == '\n' && lineLen > 0) {
      processLine();
      lineLen = 0;
    }
  }
}

const GpsFix& fix() { return gpsFix; }

bool isRunning() { return gpsRunning; }

float speedKmh() { return gpsFix.speedKnots * 1.852f; }

float speedMph() { return gpsFix.speedKnots * 1.15078f; }

} // namespace GpsService
