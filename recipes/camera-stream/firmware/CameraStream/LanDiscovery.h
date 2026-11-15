// LanDiscovery — let a laptop find this ESP32 on the LAN without knowing its IP.
// Part of esp32-recipes (https://github.com/MJ-Park/esp32-recipes), MIT License.
//
// The board announces itself on UDP 38271 ("LANDISC1 HELLO id=<team> ...").
// A laptop running host/lan_discovery.py with the same id answers "ACK" and
// learns the board's IP from the packet's source address. Broadcast first;
// if the network filters broadcast, the board unicasts every host in its /24.
//
//   #include "LanDiscovery.h"
//   LanDiscovery disc("team07");      // unique per team at the event
//   ...after WiFi is connected:
//   disc.begin();
//
// See PROTOCOL.md for the wire format.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#define LANDISC_VERSION "1.0.0"
#define LANDISC_DEFAULT_PORT 38271

class LanDiscovery {
 public:
  // id: letters, digits, '-', '_' (max 32). Empty/NULL -> "esp32-<last 6 hex of MAC>".
  explicit LanDiscovery(const char *id, uint16_t port = LANDISC_DEFAULT_PORT);

  // Extra key=value pairs included in every HELLO (e.g. "http", 80). Call before begin().
  // Returns false if the field buffer is full or the key/value contains spaces.
  bool addField(const char *key, const char *value);
  bool addField(const char *key, long value);

  // Starts a background FreeRTOS task. Safe to call before Wi-Fi connects (it waits).
  bool begin();
  void end();

  // Laptop that answered with a matching ACK.
  bool hasHost() const { return host_ != 0; }
  IPAddress hostIP() const { return IPAddress((uint32_t)host_); }
  uint32_t msSinceAck() const { return lastAckMs_ ? millis() - lastAckMs_ : UINT32_MAX; }
  const char *id() const { return id_; }

  // Called (from the discovery task) when a host is found or changes address.
  void onHost(void (*cb)(IPAddress host)) { cb_ = cb; }
  // How often to re-announce to a known host (default 30 s).
  void setHeartbeat(uint32_t ms) { heartbeatMs_ = ms < 5000 ? 5000 : ms; }
  // Serial log lines prefixed with [LanDiscovery] (default on).
  void setLog(bool on) { log_ = on; }

 private:
  static void taskEntry(void *self);
  void run();
  void sendHello(uint32_t ipNetOrder);
  bool waitAck(uint32_t ms);
  bool idMatches(const char *msg) const;
  void setHost(uint32_t ip);

  char id_[33];
  char fields_[200];
  uint16_t port_;
  int sock_ = -1;
  volatile uint32_t host_ = 0;
  volatile uint32_t lastAckMs_ = 0;
  uint32_t heartbeatMs_ = 30000;
  bool log_ = true;
  void (*cb_)(IPAddress) = nullptr;
  void *task_ = nullptr;
  volatile bool stop_ = false;
};
