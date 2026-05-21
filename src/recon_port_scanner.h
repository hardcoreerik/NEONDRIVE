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

static constexpr uint8_t RECON_MAX_HOSTS = 40;
static constexpr uint8_t RECON_MAX_OPEN_PORTS = 16;
static constexpr uint8_t RECON_MAX_SCAN_PORTS = 24;
static constexpr uint8_t RECON_MAX_EVENTS = 12;
static constexpr uint8_t RECON_RATE_SAMPLES = 32;

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

enum class ReconEventType : uint8_t {
  INFO = 0,
  DISCOVERED,
  PROBE,
  OPEN,
  TIMEOUT,
  RETRY,
  DONE,
  ERROR
};

struct ReconEventRecord {
  uint32_t ms;
  ReconEventType type;
  uint32_t ipU32;
  uint16_t port;
  uint16_t elapsedMs;
  const char* service;
  char message[28];
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
  uint8_t portCount() const { return portCount_; }
  uint16_t portAt(uint8_t idx) const { return (idx < portCount_) ? portList_[idx] : 0; }
  uint8_t scanHostIndex() const { return scanHostIndex_; }
  uint8_t scanPortIndex() const { return scanPortIndex_; }
  uint32_t totalHostCandidates() const { return totalHostCandidates_; }
  uint32_t hostsProbed() const { return hostsProbed_; }
  uint32_t portsAttempted() const { return portsAttempted_; }
  uint32_t portsTotal() const { return portsTotal_; }
  uint16_t timeoutMs() const { return timeoutMs_; }
  const char* profileLabel() const { return profileLabel_; }
  uint8_t eventCount() const { return eventCount_; }
  const ReconEventRecord* eventAt(uint8_t idx) const;
  uint8_t rateSampleCount() const { return rateSampleCount_; }
  uint16_t rateSampleAt(uint8_t idx) const;
  uint16_t latestRatePerSec() const;
  void clearResults();

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
  void pushEvent(ReconEventType type,
                 const IPAddress* ip = nullptr,
                 uint16_t port = 0,
                 uint16_t elapsedMs = 0,
                 const char* service = nullptr,
                 const char* msg = nullptr);
  void noteRateSampleTick(uint32_t now);

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
  uint32_t totalHostCandidates_ = 0;
  uint32_t hostsProbed_ = 0;
  uint32_t portsAttempted_ = 0;
  uint32_t portsTotal_ = 0;
  uint16_t timeoutMs_ = 120;
  char profileLabel_[16];
  uint32_t scanStartMs_ = 0;
  uint32_t rateWindowStartMs_ = 0;
  uint16_t rateWindowAttempts_ = 0;
  uint16_t* rateSamples_ = nullptr;
  uint8_t rateSampleHead_ = 0;
  uint8_t rateSampleCount_ = 0;
  ReconEventRecord* events_ = nullptr;
  uint8_t eventHead_ = 0;
  uint8_t eventCount_ = 0;
  char subnetLabel_[24];
  char errorMessage_[64];
};

#endif
