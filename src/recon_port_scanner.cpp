#include "recon_port_scanner.h"

#include <string.h>

uint32_t reconIpv4ToU32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

uint32_t reconSubnetBaseFromU32(uint32_t ip, uint32_t mask) {
  return ip & mask;
}

uint32_t reconBroadcastFromU32(uint32_t subnetBase, uint32_t mask) {
  return subnetBase | ~mask;
}

uint8_t reconPrefixLengthFromMaskU32(uint32_t mask) {
  uint8_t bits = 0;
  for (int i = 31; i >= 0; --i) {
    if ((mask >> i) & 0x1U) bits++;
    else break;
  }
  return bits;
}

const char* reconServiceName(uint16_t port) {
  switch (port) {
    case 20:
    case 21: return "FTP";
    case 22: return "SSH";
    case 23: return "Telnet";
    case 25: return "SMTP";
    case 53: return "DNS";
    case 80: return "HTTP";
    case 110: return "POP3";
    case 123: return "NTP";
    case 139: return "NetBIOS";
    case 143: return "IMAP";
    case 161: return "SNMP";
    case 443: return "HTTPS";
    case 445: return "SMB";
    case 502: return "MODBUS";
    case 1883: return "MQTT";
    case 3306: return "MySQL";
    case 3389: return "RDP";
    case 5432: return "PostgreSQL";
    case 5900: return "VNC";
    case 8080: return "HTTP-Alt";
    case 8443: return "HTTPS-Alt";
    case 47808: return "BACnet";
    default: return "Unknown";
  }
}

#ifdef ARDUINO

#include "recon_oui_db.h"
#include "wifiscan_helpers.h"

extern "C" {
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
}

#if __has_include("lwip/etharp.h")
extern "C" {
#include "lwip/etharp.h"
}
#define RECON_HAS_ARP_LOOKUP 1
#else
#define RECON_HAS_ARP_LOOKUP 0
#endif

static IPAddress reconU32ToIp(uint32_t ip) {
  return IPAddress((uint8_t)((ip >> 24) & 0xFF),
                   (uint8_t)((ip >> 16) & 0xFF),
                   (uint8_t)((ip >> 8) & 0xFF),
                   (uint8_t)(ip & 0xFF));
}

static uint32_t reconIpToU32(const IPAddress& ip) {
  return reconIpv4ToU32(ip[0], ip[1], ip[2], ip[3]);
}

static void reconFormatMac(const uint8_t mac[6], char* out, size_t outLen) {
  if (!out || outLen < 18) return;
  snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

ReconPortScanner::ReconPortScanner() {
  memset(subnetLabel_, 0, sizeof(subnetLabel_));
  memset(errorMessage_, 0, sizeof(errorMessage_));
  clearHosts();
}

bool ReconPortScanner::start() {
  IPAddress ip;
  IPAddress mask;
  IPAddress gateway;
  if (!wifiGetStaNetwork(ip, mask, gateway)) {
    setError("WiFi not connected");
    state_ = ReconScanState::ERROR;
    running_ = false;
    return false;
  }
  return startInternal(ip, mask, gateway);
}

bool ReconPortScanner::startInternal(const IPAddress& ip, const IPAddress& mask, const IPAddress& gateway) {
  localIp_ = ip;
  subnetMask_ = mask;
  gatewayIp_ = gateway;
  clearHosts();
  memset(errorMessage_, 0, sizeof(errorMessage_));

  static const uint16_t kDefaultPorts[] = {21, 22, 23, 80, 443, 445, 8080, 502, 47808};
  portCount_ = 0;
  for (size_t i = 0; i < sizeof(kDefaultPorts) / sizeof(kDefaultPorts[0]); ++i) {
    if (portCount_ >= RECON_MAX_SCAN_PORTS) break;
    portList_[portCount_++] = kDefaultPorts[i];
  }

  subnetBaseU32_ = reconSubnetBaseFromU32(reconIpToU32(localIp_), reconIpToU32(subnetMask_));
  broadcastU32_ = reconBroadcastFromU32(subnetBaseU32_, reconIpToU32(subnetMask_));
  subnetPrefix_ = reconPrefixLengthFromMaskU32(reconIpToU32(subnetMask_));
  firstHostU32_ = subnetBaseU32_ + 1U;
  lastHostU32_ = broadcastU32_ - 1U;

  if (firstHostU32_ > lastHostU32_) {
    setError("Invalid subnet");
    state_ = ReconScanState::ERROR;
    running_ = false;
    return false;
  }

  IPAddress subnetIp = reconU32ToIp(subnetBaseU32_);
  snprintf(subnetLabel_, sizeof(subnetLabel_), "%u.%u.%u.%u/%u",
           subnetIp[0], subnetIp[1], subnetIp[2], subnetIp[3], subnetPrefix_);

  currentHostU32_ = firstHostU32_;
  probeSentAtMs_ = 0;
  scanHostIndex_ = 0;
  scanPortIndex_ = 0;
  state_ = ReconScanState::DISCOVER_SEND;
  running_ = true;
  return true;
}

void ReconPortScanner::stop() {
  running_ = false;
  if (state_ != ReconScanState::ERROR) state_ = ReconScanState::IDLE;
}

void ReconPortScanner::setError(const char* msg) {
  snprintf(errorMessage_, sizeof(errorMessage_), "%s", msg ? msg : "Unknown scanner error");
}

void ReconPortScanner::clearHosts() {
  hostCount_ = 0;
  memset(hosts_, 0, sizeof(hosts_));
}

const ReconHostRecord* ReconPortScanner::hostAt(uint8_t idx) const {
  if (idx >= hostCount_) return nullptr;
  return &hosts_[idx];
}

bool ReconPortScanner::sendArpProbe(const IPAddress& ip) {
#if RECON_HAS_ARP_LOOKUP
  if (!netif_default) return false;
  ip4_addr_t addr;
  IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
  return (etharp_request(netif_default, &addr) == ERR_OK);
#else
  (void)ip;
  return false;
#endif
}

bool ReconPortScanner::readArpEntry(const IPAddress& ip, uint8_t outMac[6]) const {
  if (!outMac) return false;
#if RECON_HAS_ARP_LOOKUP
  if (!netif_default) return false;
  ip4_addr_t addr;
  IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* ipRet = nullptr;
  if (etharp_find_addr(netif_default, &addr, &eth, &ipRet) != ERR_OK || !eth) return false;
  memcpy(outMac, eth->addr, 6);
  return true;
#else
  (void)ip;
  memset(outMac, 0, 6);
  return false;
#endif
}

bool ReconPortScanner::addHost(const IPAddress& ip, const uint8_t mac[6], uint16_t rttMs) {
  if (hostCount_ >= RECON_MAX_HOSTS) return false;

  for (uint8_t i = 0; i < hostCount_; ++i) {
    if (hosts_[i].ip == ip) return false;
  }

  ReconHostRecord& host = hosts_[hostCount_++];
  memset(&host, 0, sizeof(host));
  host.ip = ip;
  memcpy(host.mac, mac, 6);
  host.rttMs = rttMs;
  host.openCount = 0;

  const char* vendor = reconLookupOuiVendor(host.mac);
  snprintf(host.vendor, sizeof(host.vendor), "%s", vendor ? vendor : "Unknown");
  snprintf(host.hostname, sizeof(host.hostname), "-");
  snprintf(host.osGuess, sizeof(host.osGuess), "Unknown");
  return true;
}

bool ReconPortScanner::checkTcpPort(const IPAddress& ip, uint16_t port, uint16_t timeoutMs, uint16_t& elapsedMs) const {
  WiFiClient client;
  const uint32_t started = millis();
  const bool ok = client.connect(ip, port, timeoutMs);
  const uint32_t took = millis() - started;
  elapsedMs = (took > 65535U) ? 65535U : (uint16_t)took;
  if (ok) client.stop();
  return ok;
}

void ReconPortScanner::fingerprintHost(ReconHostRecord& host) const {
  bool has445 = false;
  bool has22 = false;
  bool has80 = false;
  bool has502 = false;
  bool has47808 = false;
  bool hasPrinter = false;
  for (uint8_t i = 0; i < host.openCount; ++i) {
    uint16_t port = host.open[i].port;
    if (port == 445) has445 = true;
    if (port == 22) has22 = true;
    if (port == 80 || port == 443 || port == 8080) has80 = true;
    if (port == 502) has502 = true;
    if (port == 47808) has47808 = true;
    if (port == 9100 || port == 515 || port == 631) hasPrinter = true;
  }

  if (has502 || has47808) snprintf(host.osGuess, sizeof(host.osGuess), "ICS/Automation");
  else if (hasPrinter) snprintf(host.osGuess, sizeof(host.osGuess), "Printer");
  else if (has445) snprintf(host.osGuess, sizeof(host.osGuess), "Windows/SMB");
  else if (has22 && !has445) snprintf(host.osGuess, sizeof(host.osGuess), "Linux/Unix");
  else if (has80) snprintf(host.osGuess, sizeof(host.osGuess), "Network/IoT");
  else snprintf(host.osGuess, sizeof(host.osGuess), "Unknown");
}

void ReconPortScanner::tick() {
  if (!running_) return;

  const uint32_t now = millis();
  switch (state_) {
    case ReconScanState::DISCOVER_SEND: {
      if (currentHostU32_ > lastHostU32_) {
        if (hostCount_ == 0) {
          running_ = false;
          state_ = ReconScanState::DONE;
        } else {
          scanHostIndex_ = 0;
          scanPortIndex_ = 0;
          state_ = ReconScanState::PORT_SCAN;
        }
        return;
      }

      const IPAddress probeIp = reconU32ToIp(currentHostU32_);
      if (probeIp == localIp_) {
        currentHostU32_++;
        return;
      }
      sendArpProbe(probeIp);
      probeSentAtMs_ = now;
      state_ = ReconScanState::DISCOVER_WAIT;
      return;
    }
    case ReconScanState::DISCOVER_WAIT: {
      if ((now - probeSentAtMs_) < 30U) return;
      state_ = ReconScanState::DISCOVER_READ;
      return;
    }
    case ReconScanState::DISCOVER_READ: {
      const IPAddress probeIp = reconU32ToIp(currentHostU32_);
      uint8_t mac[6] = {0};
      if (readArpEntry(probeIp, mac)) {
        const uint32_t took = now - probeSentAtMs_;
        const uint16_t rtt = (took > 65535U) ? 65535U : (uint16_t)took;
        addHost(probeIp, mac, rtt);
      }
      currentHostU32_++;
      if (hostCount_ >= RECON_MAX_HOSTS) {
        currentHostU32_ = lastHostU32_ + 1U;
      }
      state_ = ReconScanState::DISCOVER_SEND;
      return;
    }
    case ReconScanState::PORT_SCAN: {
      if (scanHostIndex_ >= hostCount_) {
        running_ = false;
        state_ = ReconScanState::DONE;
        return;
      }

      ReconHostRecord& host = hosts_[scanHostIndex_];
      if (scanPortIndex_ >= portCount_) {
        fingerprintHost(host);
        scanHostIndex_++;
        scanPortIndex_ = 0;
        return;
      }

      const uint16_t port = portList_[scanPortIndex_];
      uint16_t elapsedMs = 0;
      const bool open = checkTcpPort(host.ip, port, 120, elapsedMs);
      if (open && host.openCount < RECON_MAX_OPEN_PORTS) {
        host.open[host.openCount++] = ReconOpenPort{port, reconServiceName(port)};
      }
      if (elapsedMs > host.rttMs) host.rttMs = elapsedMs;
      scanPortIndex_++;
      return;
    }
    case ReconScanState::DONE:
    case ReconScanState::ERROR:
      running_ = false;
      return;
    case ReconScanState::IDLE:
    default:
      return;
  }
}

uint8_t ReconPortScanner::progressPct() const {
  if (state_ == ReconScanState::IDLE) return 0;
  if (state_ == ReconScanState::DONE) return 100;
  if (state_ == ReconScanState::ERROR) return 0;

  const uint32_t discoverTotal = (lastHostU32_ >= firstHostU32_) ? (lastHostU32_ - firstHostU32_ + 1U) : 1U;
  const uint32_t discoverDone = (currentHostU32_ > firstHostU32_) ? (currentHostU32_ - firstHostU32_) : 0U;
  const uint32_t discoverDen = (discoverTotal == 0U) ? 1U : discoverTotal;
  uint32_t discoverScaled = (discoverDone * 60U) / discoverDen;
  if (discoverScaled > 60U) discoverScaled = 60U;
  const uint8_t discoverPct = (uint8_t)discoverScaled;

  if (state_ == ReconScanState::DISCOVER_SEND || state_ == ReconScanState::DISCOVER_WAIT ||
      state_ == ReconScanState::DISCOVER_READ) {
    return discoverPct;
  }

  uint32_t portTotal = (uint32_t)hostCount_ * (uint32_t)portCount_;
  if (portTotal == 0U) portTotal = 1U;
  const uint32_t portDone = (uint32_t)scanHostIndex_ * (uint32_t)portCount_ + (uint32_t)scanPortIndex_;
  uint32_t portScaled = (portDone * 40U) / portTotal;
  if (portScaled > 40U) portScaled = 40U;
  uint32_t total = (uint32_t)discoverPct + portScaled;
  if (total > 100U) total = 100U;
  return (uint8_t)total;
}

bool ReconPortScanner::exportCsv(Print& out) const {
  out.println("ip,mac,vendor,hostname,os_guess,port,service,status,rtt_ms");
  char mac[18];

  for (uint8_t i = 0; i < hostCount_; ++i) {
    const ReconHostRecord& host = hosts_[i];
    reconFormatMac(host.mac, mac, sizeof(mac));
    if (host.openCount == 0) {
      out.printf("%s,%s,%s,%s,%s,-,-,CLOSED,%u\n",
                 host.ip.toString().c_str(),
                 mac,
                 host.vendor,
                 host.hostname,
                 host.osGuess,
                 (unsigned)host.rttMs);
      continue;
    }
    for (uint8_t p = 0; p < host.openCount; ++p) {
      out.printf("%s,%s,%s,%s,%s,%u,%s,OPEN,%u\n",
                 host.ip.toString().c_str(),
                 mac,
                 host.vendor,
                 host.hostname,
                 host.osGuess,
                 (unsigned)host.open[p].port,
                 host.open[p].service ? host.open[p].service : "Unknown",
                 (unsigned)host.rttMs);
    }
  }
  return true;
}

bool ReconPortScanner::exportCsv(fs::FS& fs, const char* path) const {
  if (!path || path[0] == '\0') return false;
  File f = fs.open(path, FILE_WRITE);
  if (!f) return false;
  const bool ok = exportCsv(f);
  f.close();
  return ok;
}

#endif
