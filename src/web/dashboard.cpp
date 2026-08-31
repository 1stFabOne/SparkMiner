/*
 * SparkMiner - Web Dashboard Implementation
 * GPL v3 License
 */

#include "dashboard.h"
#include "dashboard_html.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <board_config.h>

#include "../mining/miner.h"
#include "../stratum/stratum.h"
#include "../config/nvs_config.h"
#include "../stats/live_stats.h"
#include "../net/wifi_multi.h"

static WebServer s_server(80);
static bool s_running = false;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_httpMutex = NULL;

// --------------------------------------------
// Hashrate computation (EMA smoothed, local)
// --------------------------------------------
static uint64_t s_lastHashes = 0;
static uint32_t s_lastHashTime = 0;
static double s_smoothedHashRate = 0.0;
static bool s_firstSample = true;

static double computeHashRate() {
    mining_stats_t *mstats = miner_get_stats();
    uint32_t now = millis();
    uint32_t elapsed = now - s_lastHashTime;

    if (elapsed >= 1000) {
        uint64_t delta = mstats->hashes - s_lastHashes;
        double instant = (double)delta * 1000.0 / elapsed;
        const double alpha = 0.15;
        if (s_firstSample) {
            s_smoothedHashRate = instant;
            s_firstSample = false;
        } else {
            s_smoothedHashRate = alpha * instant + (1.0 - alpha) * s_smoothedHashRate;
        }
        s_lastHashes = mstats->hashes;
        s_lastHashTime = now;
    }
    return s_smoothedHashRate;
}

// --------------------------------------------
// JSON endpoint
// --------------------------------------------
static void handleStats() {
    StaticJsonDocument<2048> doc;

    mining_stats_t *m = miner_get_stats();
    mining_persistence_t *p = nvs_stats_get();
    miner_config_t *cfg = nvs_config_get();

    // Miner / session stats
    doc["hashRate"] = computeHashRate();
    doc["shares"] = m->shares;
    doc["accepted"] = m->accepted;
    doc["rejected"] = m->rejected;
    doc["blocks"] = m->blocks;
    doc["templates"] = m->templates;
    doc["bestDifficulty"] = m->bestDifficulty;
    doc["poolDifficulty"] = miner_get_difficulty();
    doc["avgLatency"] = m->avgLatency;
    doc["uptimeSeconds"] = (millis() - m->startTime) / 1000;

    // Wallet / pool identity
    doc["wallet"] = cfg->wallet;
    doc["worker"] = cfg->workerName;

    // Stratum connection state
    doc["poolConnected"] = stratum_is_connected();

    // WiFi + system
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["freeHeap"] = ESP.getFreeHeap() / 1024.0;

    // IP address (station or fallback AP)
    doc["ip"] = wifimulti_get_ip();

    // Live stats (thread-safe copy) - network / pool / price
    live_stats_t ls;
    live_stats_get_copy(&ls);

    doc["poolName"] = stratum_get_pool();
    if (ls.poolValid && ls.poolName[0]) doc["poolName"] = ls.poolName;
    int failovers = ls.failovers;
    if (stratum_is_backup()) failovers++;
    doc["failovers"] = failovers;

    if (ls.blockValid) {
        doc["blockHeight"] = ls.blockHeight;
    } else {
        doc["blockHeight"] = 0;
    }
    if (ls.networkValid) {
        doc["networkHashrate"] = ls.networkHashrate;
        doc["networkDifficulty"] = ls.networkDifficulty;
    } else {
        doc["networkHashrate"] = "";
        doc["networkDifficulty"] = "";
    }
    if (ls.priceValid) {
        doc["btcPriceUsd"] = ls.btcPriceUsd;
    } else {
        doc["btcPriceUsd"] = 0;
    }

    char buf[2048];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.send(200, "application/json", String(buf, len));
}

// --------------------------------------------
// HTML endpoint
// --------------------------------------------
static void handleRoot() {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, "text/html", DASHBOARD_HTML);
}

static void handleNotFound() {
    s_server.send(404, "text/plain", "Not Found - use / or /api/stats");
}

// --------------------------------------------
// WiFi management endpoints
// --------------------------------------------
static void handleWifiList() {
    miner_config_t *cfg = nvs_config_get();
    StaticJsonDocument<1024> doc;

    JsonArray nets = doc.createNestedArray("networks");
    const char* currentSsid = wifimulti_get_ssid();
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++) {
        if (cfg->wifiNetworks[i].ssid[0] == '\0') continue;
        JsonObject n = nets.createNestedObject();
        n["ssid"] = cfg->wifiNetworks[i].ssid;
        bool isActive = (currentSsid[0] != '\0') &&
                        (strcmp(currentSsid, cfg->wifiNetworks[i].ssid) == 0);
        n["active"] = isActive;
    }

    doc["connected"] = wifimulti_is_connected();
    doc["ap_mode"] = wifimulti_is_ap_mode();
    doc["ip"] = wifimulti_get_ip();
    doc["current_ssid"] = currentSsid;
    doc["max"] = MAX_WIFI_NETWORKS;

    char buf[1024];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.send(200, "application/json", String(buf, len));
}

static void handleWifiAdd() {
    StaticJsonDocument<512> body;
    deserializeJson(body, s_server.arg("plain"));
    const char* ssid = body["ssid"] | "";
    const char* pw = body["password"] | "";

    if (ssid[0] == '\0') {
        s_server.send(400, "text/plain", "missing ssid");
        return;
    }

    miner_config_t *cfg = nvs_config_get();
    wifimulti_lock();

    // Dedup
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++) {
        if (strcmp(cfg->wifiNetworks[i].ssid, ssid) == 0) {
            nvs_config_save(cfg);
            wifimulti_unlock();
            wifimulti_rescan();
            s_server.send(200, "text/plain", "exists");
            return;
        }
    }

    if (cfg->wifiNetworkCount >= MAX_WIFI_NETWORKS) {
        wifimulti_unlock();
        s_server.send(400, "text/plain", "max 5 networks reached");
        return;
    }

    wifi_network_t *wn = &cfg->wifiNetworks[cfg->wifiNetworkCount++];
    strncpy(wn->ssid, ssid, MAX_SSID_LENGTH);
    wn->ssid[MAX_SSID_LENGTH] = '\0';
    strncpy(wn->password, pw, MAX_PASSWORD_LEN);
    wn->password[MAX_PASSWORD_LEN] = '\0';

    nvs_config_save(cfg);
    wifimulti_unlock();
    wifimulti_rescan();
    Serial.printf("[DASHBOARD] WiFi network added: %s\n", wn->ssid);
    s_server.send(200, "text/plain", "ok");
}

static void handleWifiDelete() {
    String ssid = s_server.arg("ssid");
    if (ssid.length() == 0) {
        s_server.send(400, "text/plain", "missing ssid");
        return;
    }

    miner_config_t *cfg = nvs_config_get();
    wifimulti_lock();
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++) {
        if (strcmp(cfg->wifiNetworks[i].ssid, ssid.c_str()) == 0) {
            cfg->wifiNetworks[i] = cfg->wifiNetworks[cfg->wifiNetworkCount - 1];
            cfg->wifiNetworkCount--;
            nvs_config_save(cfg);
            wifimulti_unlock();
            wifimulti_rescan();
            Serial.printf("[DASHBOARD] WiFi network removed: %s\n", ssid.c_str());
            s_server.send(200, "text/plain", "ok");
            return;
        }
    }
    wifimulti_unlock();
    s_server.send(404, "text/plain", "not found");
}

// --------------------------------------------
// Public API
// --------------------------------------------
void dashboard_start() {
    if (s_running) return;

    if (s_httpMutex == NULL) {
        s_httpMutex = xSemaphoreCreateMutex();
    }

    s_server.on("/", handleRoot);
    s_server.on("/api/stats", HTTP_GET, handleStats);
    s_server.on("/api/wifi", HTTP_GET, handleWifiList);
    s_server.on("/api/wifi", HTTP_POST, handleWifiAdd);
    s_server.on("/api/wifi", HTTP_DELETE, handleWifiDelete);
    s_server.onNotFound(handleNotFound);
    s_server.begin();

    s_running = true;
    Serial.printf("[DASHBOARD] Web server started at http://%s\n",
                  wifimulti_get_ip());
}

bool dashboard_is_running() {
    return s_running;
}

void dashboard_task(void *param) {
    Serial.printf("[DASHBOARD] Task started on core %d\n", xPortGetCoreID());

    // Wait up to ~20s for station WiFi connection OR fallback AP before starting
    unsigned long waitStart = millis();
    while (!wifimulti_is_connected() && !wifimulti_is_ap_mode()
           && (millis() - waitStart) < 20000) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (wifimulti_is_connected() || wifimulti_is_ap_mode()) {
        dashboard_start();
    } else {
        Serial.println("[DASHBOARD] No network/AP available - dashboard disabled");
    }

    for (;;) {
        if (s_httpMutex) xSemaphoreTake(s_httpMutex, portMAX_DELAY);
        if (s_running) {
            s_server.handleClient();
        }
        if (s_httpMutex) xSemaphoreGive(s_httpMutex);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
