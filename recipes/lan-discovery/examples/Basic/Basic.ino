// LanDiscovery / Basic
// Connects to Wi-Fi, then lets a laptop find this board automatically.
// Laptop:  python3 lan_discovery.py --id team00
//
// Using the library without installing it: copy LanDiscovery.h and
// LanDiscovery.cpp into this sketch folder (next to Basic.ino).

#include <WiFi.h>
#include "LanDiscovery.h"

const char *WIFI_SSID = "";       // empty = use the Wi-Fi already saved on this board
const char *WIFI_PASS = "";
const char *TEAM_ID = "team00";   // unique per team at the event (letters, digits, - _)

LanDiscovery disc(TEAM_ID);

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  if (strlen(WIFI_SSID)) WiFi.begin(WIFI_SSID, WIFI_PASS);
  else WiFi.begin();  // credentials saved by an earlier sketch

  Serial.print("WiFi connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
    if (millis() - t0 > 20000) {
      Serial.println("\nStill not connected: set WIFI_SSID / WIFI_PASS at the top of the sketch.");
      t0 = millis();
    }
  }
  Serial.printf("\nWiFi connected, ip=%s\n", WiFi.localIP().toString().c_str());

  disc.addField("http", 80);  // optional: extra info the laptop will see
  disc.begin();               // runs in the background from here on
}

void loop() {
  static bool printed = false;
  if (disc.hasHost() && !printed) {
    Serial.printf("Laptop found me: %s\n", disc.hostIP().toString().c_str());
    printed = true;
  }
  delay(500);
}
