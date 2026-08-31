/*
 * SparkMiner - Multi-Network WiFi Controller Implementation
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <board_config.h>
#include "config/nvs_config.h"
#include "wifi_multi.h"

#define AP_SSID_SUFFIX_LEN 8   // last 6 hex chars of MAC, sanitized

static uint32_t s_lastAttempt = 0;
static bool s_apMode = false;
static char s_ip[16] = "0.0.0.0";
static char s_ssid[MAX_SSID_LENGTH + 1] = "";

// Delay between auto-reconnect / AP re-evaluation attempts
#define RECONNECT_INTERVAL_MS 30000UL
// Timeout for each connect() call before giving up on a network
#define CONNECT_TIMEOUT_MS 10000UL

static void syncIp() {
    if (s_apMode || (WiFi.getMode() & WIFI_AP)) {
        IPAddress apIp = WiFi.softAPIP();
        snprintf(s_ip, sizeof(s_ip), "%d.%d.%d.%d", apIp[0], apIp[1], apIp[2], apIp[3]);
    } else if (WiFi.status() == WL_CONNECTED) {
        snprintf(s_ip, sizeof(s_ip), "%s", WiFi.localIP().toString().c_str());
    } else {
        strcpy(s_ip, "0.0.0.0");
    }
}

static void updateSsid() {
    if (WiFi.status() == WL_CONNECTED) {
        String ssid = WiFi.SSID();
        strncpy(s_ssid, ssid.c_str(), MAX_SSID_LENGTH);
        s_ssid[MAX_SSID_LENGTH] = '\0';
    } else {
        s_ssid[0] = '\0';
    }
}

/**
 * Try to connect to the first saved network that is in range.
 * Returns true if connected.
 */
static bool tryConnectNext() {
    miner_config_t *c = nvs_config_get();
    if (c->wifiNetworkCount == 0) return false;

    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n < 0) {
        Serial.printf("[WIFI-MULTI] Scan failed (%d), retrying later\n", n);
        return false;
    }

    for (uint8_t i = 0; i < c->wifiNetworkCount; i++) {
        const char* wanted = c->wifiNetworks[i].ssid;
        if (wanted[0] == '\0') continue;
        for (int j = 0; j < n; j++) {
            if (strcmp(wanted, WiFi.SSID(j).c_str()) == 0) {
                Serial.printf("[WIFI-MULTI] Found '%s', connecting...\n", wanted);
                WiFi.begin(c->wifiNetworks[i].ssid, c->wifiNetworks[i].password);
                unsigned long t0 = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - t0) < CONNECT_TIMEOUT_MS) {
                    delay(100);
                }
                WiFi.scanDelete();
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("[WIFI-MULTI] Connected to '%s' (%s)\n",
                                  wanted, WiFi.localIP().toString().c_str());
                    return true;
                }
                Serial.printf("[WIFI-MULTI] Failed to connect to '%s', trying next\n", wanted);
                break;
            }
        }
    }

    WiFi.scanDelete();
    return false;
}

static void startFallbackAp() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    if (mac.length() < AP_SSID_SUFFIX_LEN) {
        mac = "BADMAC";
    } else {
        mac = mac.substring(mac.length() - AP_SSID_SUFFIX_LEN);
    }
    String apSsid = String(AP_SSID_PREFIX) + mac;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
    s_apMode = true;
    Serial.printf("[WIFI-MULTI] Fallback AP '%s' active at %s\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    syncIp();
}

void wifimulti_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // keep radio responsive for mining/reconnect

    if (MDNS.begin("sparkminer")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[WIFI-MULTI] mDNS started: http://sparkminer.local");
    } else {
        Serial.println("[WIFI-MULTI] mDNS start failed");
    }

    if (tryConnectNext()) {
        s_apMode = false;
        syncIp();
        updateSsid();
    } else {
        startFallbackAp();
    }
}

void wifimulti_loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (s_apMode) {
            // Recovered to a station network - drop the fallback AP
            s_apMode = false;
            WiFi.softAPdisconnect(true);
            Serial.println("[WIFI-MULTI] Reconnected to station network, AP stopped");
            syncIp();
            updateSsid();
        }
        return;
    }

    if (millis() - s_lastAttempt < RECONNECT_INTERVAL_MS) {
        return;
    }
    s_lastAttempt = millis();

    if (!s_apMode) {
        // Lost the station connection - try to reconnect
        if (tryConnectNext()) {
            syncIp();
            updateSsid();
        } else {
            startFallbackAp();
        }
    } else {
        // In AP mode - periodically check whether a saved network came back in range
        if (tryConnectNext()) {
            s_apMode = false;
            WiFi.softAPdisconnect(true);
            syncIp();
            updateSsid();
            Serial.println("[WIFI-MULTI] Switched AP -> station network");
        }
    }
}

bool wifimulti_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool wifimulti_is_ap_mode() {
    return s_apMode;
}

const char* wifimulti_get_ip() {
    syncIp();
    return s_ip;
}

const char* wifimulti_get_ssid() {
    updateSsid();
    return s_ssid;
}

void wifimulti_rescan() {
    s_lastAttempt = 0;
}
