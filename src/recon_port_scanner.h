#pragma once

#include <stdint.h>

uint32_t reconIpv4ToU32(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
uint32_t reconSubnetBaseFromU32(uint32_t ip, uint32_t mask);
uint32_t reconBroadcastFromU32(uint32_t subnetBase, uint32_t mask);
uint8_t reconPrefixLengthFromMaskU32(uint32_t mask);
const char* reconServiceName(uint16_t port);

#ifdef ARDUINO
#include <Arduino.h>
#include <FS.h>
#include <IPAddress.h>
#include <WiFi.h>

static constexpr uint8_t RECON_MAX_HOSTS = 50;
static constexpr uint8_t RECON_MAX_OPEN_PORTS = 16;
static constexpr uint8_t RECON_MAX_SCAN_PORTS = 24;

enum class ReconScanState : uint8_t {
  IDLE = 0,
  DISCOVER_SEND,
  DISCOVER_WAIT,
  DISCOVER_READ,
  PORT_SCAN,
  DONE,
  ERROR
};

struct ReconOpenPort {
  uint16_t port;
  const char* service;
};

struct ReconHostRecord {
  IPAddress ip;
  uint8_t mac[6];
  char vendor[32];
  char hostname[32];
  char osGuess[32];
  uint16_t rttMs;
  uint8_t openCount;
  ReconOpenPort open[RECON_MAX_OPEN_PORTS];
};

class ReconPortScanner {
public:
  ReconPortScanner();

  bool start(bool deep = false);
  void stop();
  void tick();

  bool isRunning() const { return running_; }
  ReconScanState state() const { return state_; }
  uint8_t progressPct() const;
  uint8_t hostCount() const { return hostCount_; }
  const ReconHostRecord* hostAt(uint8_t idx) const;
  const char* subnetLabel() const { return subnetLabel_; }
  const char* errorMessage() const { return errorMessage_; }

  bool exportCsv(Print& out) const;
  bool exportCsv(fs::FS& fs, const char* path = "/recon_scan.csv") const;

private:
  bool startInternal(const IPAddress& ip, const IPAddress& mask, const IPAddress& gateway, bool deep);
  void setError(const char* msg);
  bool checkTcpPort(const IPAddress& ip, uint16_t port, uint16_t timeoutMs, uint16_t& elapsedMs) const;
  bool sendArpProbe(const IPAddress& ip);
  bool readArpEntry(const IPAddress& ip, uint8_t outMac[6]) const;
  bool addHost(const IPAddress& ip, const uint8_t mac[6], uint16_t rttMs);
  void fingerprintHost(ReconHostRecord& host) const;
  void clearHosts();

  ReconScanState state_ = ReconScanState::IDLE;
  bool running_ = false;

  IPAddress localIp_;
  IPAddress subnetMask_;
  IPAddress gatewayIp_;

  uint32_t subnetBaseU32_ = 0;
  uint32_t broadcastU32_ = 0;
  uint32_t firstHostU32_ = 0;
  uint32_t lastHostU32_ = 0;
  uint32_t currentHostU32_ = 0;
  uint32_t probeSentAtMs_ = 0;
  uint8_t subnetPrefix_ = 24;

  uint16_t portList_[RECON_MAX_SCAN_PORTS];
  uint8_t portCount_ = 0;

  uint8_t hostCount_ = 0;
  ReconHostRecord hosts_[RECON_MAX_HOSTS];

  uint8_t scanHostIndex_ = 0;
  uint8_t scanPortIndex_ = 0;
  char subnetLabel_[24];
  char errorMessage_[64];
};

#endif
