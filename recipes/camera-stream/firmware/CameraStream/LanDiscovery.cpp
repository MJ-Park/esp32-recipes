// LanDiscovery — see LanDiscovery.h and PROTOCOL.md. MIT License.
#include "LanDiscovery.h"

#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define LD_LOG(...)                       \
  do {                                    \
    if (log_) {                           \
      Serial.print("[LanDiscovery] ");    \
      Serial.printf(__VA_ARGS__);         \
      Serial.println();                   \
    }                                     \
  } while (0)

static bool validToken(const char *s) {
  if (!s || !*s) return false;
  for (; *s; s++)
    if (*s == ' ' || *s == '=' || *s < 0x21 || *s > 0x7e) return false;
  return true;
}

LanDiscovery::LanDiscovery(const char *id, uint16_t port) : port_(port) {
  id_[0] = 0;
  fields_[0] = 0;
  if (id) {
    size_t n = 0;
    for (const char *p = id; *p && n < sizeof(id_) - 1; p++) {
      char c = *p;
      id_[n++] = (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '-';
    }
    id_[n] = 0;
  }
}

bool LanDiscovery::addField(const char *key, const char *value) {
  if (!validToken(key) || !validToken(value)) return false;
  size_t used = strlen(fields_);
  int n = snprintf(fields_ + used, sizeof(fields_) - used, " %s=%s", key, value);
  if (n < 0 || used + n >= sizeof(fields_)) {
    fields_[used] = 0;
    return false;
  }
  return true;
}

bool LanDiscovery::addField(const char *key, long value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld", value);
  return addField(key, buf);
}

bool LanDiscovery::begin() {
  if (task_) return true;
  stop_ = false;
  TaskHandle_t h = nullptr;
  if (xTaskCreatePinnedToCore(taskEntry, "landisc", 4096, this, 1, &h, 0) != pdPASS) return false;
  task_ = h;
  return true;
}

void LanDiscovery::end() {
  if (!task_) return;
  stop_ = true;
  for (int i = 0; i < 40 && task_; i++) delay(50);
}

void LanDiscovery::taskEntry(void *self) {
  static_cast<LanDiscovery *>(self)->run();
  static_cast<LanDiscovery *>(self)->task_ = nullptr;
  vTaskDelete(NULL);
}

void LanDiscovery::setHost(uint32_t ip) {
  lastAckMs_ = millis();
  if (ip == host_) return;
  host_ = ip;
  LD_LOG("host %s (id=%s)", IPAddress(ip).toString().c_str(), id_);
  if (cb_) cb_(IPAddress(ip));
}

void LanDiscovery::sendHello(uint32_t ipNetOrder) {
  char msg[360];
  int n = snprintf(msg, sizeof(msg), "LANDISC1 HELLO id=%s mac=%s ip=%s rssi=%d lib=%s%s", id_,
                   WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
                   LANDISC_VERSION, fields_);
  struct sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(port_);
  to.sin_addr.s_addr = ipNetOrder;
  sendto(sock_, msg, n, 0, (struct sockaddr *)&to, sizeof(to));
}

// True if the message carries id=<our id> (or id=* for WHO).
bool LanDiscovery::idMatches(const char *msg) const {
  const char *p = strstr(msg, " id=");
  if (!p) return false;
  p += 4;
  size_t len = strcspn(p, " \r\n");
  if (len == 1 && *p == '*') return true;
  return len == strlen(id_) && strncmp(p, id_, len) == 0;
}

// Wait up to `ms` for a matching ACK; answers WHO probes meanwhile.
bool LanDiscovery::waitAck(uint32_t ms) {
  uint32_t start = millis();
  char buf[128];
  while (!stop_) {
    uint32_t elapsed = millis() - start;
    if (elapsed >= ms) break;
    uint32_t slice = ms - elapsed > 100 ? 100 : ms - elapsed;  // wake up to check stop_
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(sock_, &rf);
    struct timeval tv = {0, (long)slice * 1000};
    if (select(sock_ + 1, &rf, NULL, NULL, &tv) <= 0) continue;
    struct sockaddr_in from = {};
    socklen_t fl = sizeof(from);
    int n = recvfrom(sock_, buf, sizeof(buf) - 1, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
    if (n <= 0) continue;
    buf[n] = 0;
    if (!strncmp(buf, "LANDISC1 ACK", 12) && idMatches(buf)) {
      setHost(from.sin_addr.s_addr);
      return true;
    }
    if (!strncmp(buf, "LANDISC1 WHO", 12) && idMatches(buf)) sendHello(from.sin_addr.s_addr);
  }
  return false;
}

void LanDiscovery::run() {
  sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int one = 1;
  setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in me = {};
  me.sin_family = AF_INET;
  me.sin_port = htons(port_);
  me.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock_, (struct sockaddr *)&me, sizeof(me)) != 0) LD_LOG("bind UDP %u failed", port_);

  int misses = 0, sweeps = 0;
  uint32_t lastSweep = 0, backoff = 10000;

  while (!stop_) {
    if (WiFi.status() != WL_CONNECTED) {
      host_ = 0;
      backoff = 10000;
      delay(500);
      continue;
    }
    if (!id_[0]) {  // default id from MAC
      String mac = WiFi.macAddress();
      mac.replace(":", "");
      snprintf(id_, sizeof(id_), "esp32-%s", mac.substring(6).c_str());
      LD_LOG("no id given, using %s", id_);
    }

    if (host_) {  // known host: unicast heartbeat, forget it after 3 misses
      sendHello(host_);
      if (waitAck(1000)) {
        misses = 0;
        waitAck(heartbeatMs_ - 1000);
      } else if (++misses >= 3) {
        LD_LOG("host %s stopped answering", hostIP().toString().c_str());
        host_ = 0;
        misses = 0;
      }
      continue;
    }

    uint32_t ip = (uint32_t)WiFi.localIP();  // network byte order
    uint32_t mask = (uint32_t)WiFi.subnetMask();

    // 1) broadcast: one packet reaches every laptop on the subnet
#ifndef LANDISC_SKIP_BROADCAST  // test hook: build with -DLANDISC_SKIP_BROADCAST to exercise the sweep
    for (int i = 0; i < 2 && !host_ && !stop_; i++) {
      sendHello((ip & mask) | ~mask);
      sendHello(INADDR_BROADCAST);
      waitAck(700);
    }
#endif

    // 2) unicast sweep of our /24, for networks that filter broadcast.
    //    Rate-limited: twice at start, then at most every 5 minutes, so dozens of
    //    boards at an event don't flood the Wi-Fi with ARP requests.
    if (!host_ && !stop_ && (sweeps < 2 || millis() - lastSweep > 300000)) {
      LD_LOG("no answer to broadcast, sweeping /24");
      uint32_t base = ntohl(ip) & 0xFFFFFF00;
      for (int h = 1; h <= 254 && !host_ && !stop_; h++) {
        uint32_t target = htonl(base | h);
        if (target != ip) sendHello(target);
        waitAck(8);  // pace for the ARP table; also catches early ACKs
      }
      if (!host_) waitAck(1500);
      sweeps++;
      lastSweep = millis();
    }

    if (!host_) {  // keep answering WHO probes while backing off
      waitAck(backoff);
      backoff = backoff * 2 > 60000 ? 60000 : backoff * 2;
    } else {
      backoff = 10000;
    }
  }
  close(sock_);
  sock_ = -1;
}
