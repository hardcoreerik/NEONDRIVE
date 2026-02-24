#pragma once

// Small glue header to expose AP globals and scanning helpers without
// colliding with the existing WiFiScan.h (case-insensitive filesystems).
#include "ap_record.h"

extern ApRecord aps[MAX_APS];
extern int apCount;
extern int apScroll;
extern int apSelected;
extern bool wifiIsScanning;

bool bssidEquals(const String& a, const String& b);
void sortApsByRssiDesc();
void dedupeKeepStrongest();
void doWifiScanBlocking();
bool startWifiScanAsync();
int pollWifiScanAsync();
