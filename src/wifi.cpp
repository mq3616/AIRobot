#include "app_wifi.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"
#include "display.h"
#include "time_sync.h"

bool wifi_connect() {
  if (strlen(AppConfig::WIFI_SSID_VALUE) == 0) {
    Serial.println("WiFi skipped: SSID is empty");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("WiFi connecting to: %s\n", AppConfig::WIFI_SSID_VALUE);
  display_set_status(DisplayStatus::WifiConnecting, AppConfig::WIFI_SSID_VALUE);
  WiFi.mode(WIFI_STA);
  WiFi.begin(AppConfig::WIFI_SSID_VALUE, AppConfig::WIFI_PASSWORD_VALUE);

  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start_ms) < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed");
    display_set_wifi_connected(false);
    display_set_status(DisplayStatus::WifiDisconnected, "connect failed");
    return false;
  }

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  display_set_wifi_connected(true);
  display_set_ip(WiFi.localIP().toString());
  display_set_status(DisplayStatus::WifiConnected, WiFi.SSID());
  time_sync_begin();
  return true;
}

bool wifi_is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifi_print_status() {
  if (!wifi_is_connected()) {
    Serial.println("WiFi status: disconnected");
    display_set_wifi_connected(false);
    display_set_status(DisplayStatus::WifiDisconnected, "");
    return;
  }

  Serial.printf("WiFi status: connected to %s, IP=%s, RSSI=%d dBm\n",
                WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  display_set_wifi_connected(true);
  display_set_ip(WiFi.localIP().toString());
  display_set_status(DisplayStatus::WifiConnected, WiFi.SSID());
}
