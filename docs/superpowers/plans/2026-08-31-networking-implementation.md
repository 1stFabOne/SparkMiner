# SparkMiner Netzwerk-Robustheit & Fernzugriff — Implementierungsplan

> **Für agentische Arbeiter:** Erforderliche Sub-Skill: superpowers:executing-plans (oder subagent-driven-development). Schritte nutzen `- [x]`-Checkbox-Syntax.

**Ziel:** Dem headless ESP32 einen stabilen mDNS-Namen, eine WLAN-Liste (bis 5) mit Auto-Reconnect, WLAN-Verwaltung im Dashboard und einen Fallback-AP mit Dashboard geben; Fernzugriff über Tailscale (Infrastruktur, kein ESP-Code).

**Architektur:** Neues Modul `src/net/wifi_multi.h/.cpp` ersetzt das Single-SSID-Modell von `wifi_manager`. Die `miner_config_t`-Struktur bekommt ein WLAN-Array (max 5) statt Einzel-`ssid`/`wifiPassword`. Das Dashboard bekommt ein Netzwerk-Panel + `/api/wifi`-Endpoints. mDNS startet nach WLAN-Verbindung. Bei "kein bekanntes WLAN" startet ein Fallback-AP, auf dem der Webserver weiterläuft.

**Tech-Stack:** ESP32-Arduino-Core (WiFi, ESPmDNS, WebServer), ArduinoJson 6.21.6, PlatformIO (esp32-headless env). Keine neuen externen Libs.

## Globale Constraints

- Projekt liegt in `C:\SecondBrain\tmp\SparkMiner` (NICHT im Vault-Repo; keine Vault-/Shop-Integration).
- Build/Flash: `pio run -e esp32-headless` / `-t upload --upload-port COM7`.
- Das ESP32-headless-Env nutzt `lib_ignore = TFT_eSPI OpenFontRender SD SD_MMC FastLED`, `arduino`-Framework.
- NVS.Persistenz ist JSON-basiert (ArduinoJson) mit Checksum; config wird auch auf SD gelesen — beide Pfade müssen den neuen WLAN-Array verstehen bzw. tolerant sein.
- `miner_config_t` lebt in `src/config/nvs_config.h`; persistiert via `src/config/nvs_config.cpp`.
- Dashboard-Module in `src/web/` (dashboard.h/.cpp + dashboard_html.h), bereits live.
- Inspektor-Veto (AGENTS.md-Regel 6) vor jedem Flash; Build muss `[SUCCESS]` zeigen.
- Proxy-/Tunnel-Teil ist reine Infrastruktur-Anleitung (Tailscale auf 24/7-Desktop-PC), kein ESP-Code.

---

### Task 1: WLAN-Array in `miner_config_t` + NVS-JSON


**Files:**
- Modify: `src/config/nvs_config.h` (Struktur)
- Modify: `src/config/nvs_config.cpp` (Laden/Speichern aus JSON lesen/schreiben)
- Test: Build `pio run -e esp32-headless` fehlerfrei + Serial-Log zeigt kompatible Config

**Interfaces:**
- Produces: `miner_config_t.wifiNetworks` — `typedef struct { char ssid[MAX_SSID_LENGTH+1]; char password[MAX_PASSWORD_LEN+1]; } wifi_network_t;` als Feld `wifi_network_t wifiNetworks[MAX_WIFI_NETWORKS];` mit `uint8_t wifiNetworkCount;` (`MAX_WIFI_NETWORKS=5`). Einzel-Felder `ssid`/`wifiPassword` werden entfernt.

- [x] **Schritt 1: Struktur ändern**

In `src/config/nvs_config.h` definieren und das Feld ersetzen:

```c
#define MAX_WIFI_NETWORKS 5

typedef struct {
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LEN + 1];
} wifi_network_t;

typedef struct {
    uint8_t wifiNetworkCount;                       // Anzahl gespeicherter Netze
    wifi_network_t wifiNetworks[MAX_WIFI_NETWORKS]; // Liste (Index 0 = primär)
    // … restliche Felder unverändert …
    // ENTFERNT: char ssid[]; char wifiPassword[];
    uint32_t checksum;
} miner_config_t;
```

- [x] **Schritt 2: JSON-Laden in `src/config/nvs_config.cpp` erweitern**

In der SD-/JSON-Ladesektion (ca. Zeile 205-208) den Einzel-`ssid`-Code ersetzen durch eine Schleife über das Array `wifi_networks`:

```cpp
// Netzwerke aus JSON lesen (Array-Einträge)
config->wifiNetworkCount = 0;
JsonArray nets = doc["wifi_networks"].as<JsonArray>();
for (JsonObject n : nets) {
    if (config->wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
    wifi_network_t *wn = &config->wifiNetworks[config->wifiNetworkCount];
    safeStrCpy(wn->ssid, n["ssid"] | "", sizeof(wn->ssid));
    safeStrCpy(wn->password, n["password"] | "", sizeof(wn->password));
    if (wn->ssid[0]) config->wifiNetworkCount++;
}
```

- [x] **Schritt 3: JSON-Speichern ergänzen**

In der Speicher-Funktion (serialize nach NVS/JSON) ein `wifi_networks`-Array schreiben. Beispiel (analog zur `config.json`-Schreibseite):

```cpp
JsonArray nets = doc.createNestedArray("wifi_networks");
for (uint8_t i = 0; i < config->wifiNetworkCount; i++) {
    JsonObject n = nets.createNestedObject();
    n["ssid"] = config->wifiNetworks[i].ssid;
    n["password"] = config->wifiNetworks[i].password;
}
```

- [x] **Schritt 4: Build prüfen**

Run: `pio run -e esp32-headless`
Expected: `[SUCCESS]` (keine Fehler zu `ssid`/`wifiPassword` mehr in nvs_config).

- [x] **Schritt 5: Committen**

```bash
git add src/config/nvs_config.h src/config/nvs_config.cpp
git commit -m "feat(config): WLAN-Array (bis 5) statt Einzel-SSID in miner_config_t"
```

---

### Task 2: Neues Modul `wifi_multi` (Liste, Auto-Reconnect, Fallback-AP)

**Files:**
- Create: `src/net/wifi_multi.h`, `src/net/wifi_multi.cpp`
- Test: Build + Serial-Log (`[WIFI-MULTI]`)

**Interfaces:**
- Consumes: `miner_config_t.wifiNetworks[]`, `wifiNetworkCount` (Task 1).
- Produces:
  - `void wifimulti_init()` — mDNS `sparkminer` + ersten Connect-Versuch.
  - `void wifimulti_loop()` — Reconnect-Zyklus (alle 30s ohne Verbindung), Fallback-AP-Logik; aus `loop()` aufrufbar (nichtblockierend).
  - `bool wifimulti_is_connected()` — Station-Verbindungsstatus.
  - `const char* wifimulti_get_ip()` — aktuelle IP (Station oder AP 192.168.4.1).
  - `void wifimulti_rescan()` — scannt erneut nach kurzem Delay.
  - `bool wifimulti_is_ap_mode()` — true wenn Fallback-AP aktiv.
  - Fallback-AP: SSID `SparkMiner_C2D8` (aus `AP_SSID_PREFIX`+MAC), Passwort `AP_PASSWORD`, IP `192.168.4.1`.

- [x] **Schritt 1: `src/net/wifi_multi.h` anlegen**

```cpp
#ifndef WIFI_MULTI_H
#define WIFI_MULTI_H
#include <Arduino.h>

void wifimulti_init();
void wifimulti_loop();
bool wifimulti_is_connected();
bool wifimulti_is_ap_mode();
const char* wifimulti_get_ip();
void wifimulti_rescan();
#endif
```

- [x] **Schritt 2: `src/net/wifi_multi.cpp` implementieren**

Kernlogik (Scan über gespeicherte Netze, erste Treffer-Verbindung, mDNS, Fallback-AP, 30s-Reconnect). Pseudocode-Rumpf, im Code konkret ausformulieren:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <board_config.h>
#include "config/nvs_config.h"
#include "wifi_multi.h"

static uint32_t s_lastAttempt = 0;
static bool s_apMode = false;
static char s_ip[16] = "0.0.0.0";

static void syncIp() {
    if (s_apMode) strcpy(s_ip, "192.168.4.1");
    else if (WiFi.status() == WL_CONNECTED) strcpy(s_ip, WiFi.localIP().toString().c_str());
    else strcpy(s_ip, "0.0.0.0");
}

static bool tryConnectNext() {
    miner_config_t *c = nvs_config_get();
    // 1) Scanne verfügbare BSSIDs
    int n = WiFi.scanNetworks();
    for (uint8_t i = 0; i < c->wifiNetworkCount; i++) {
        for (int j = 0; j < n; j++) {
            if (strcmp(c->wifiNetworks[i].ssid, WiFi.SSID(j).c_str()) == 0) {
                WiFi.begin(c->wifiNetworks[i].ssid, c->wifiNetworks[i].password);
                unsigned long t = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - t) < 10000) delay(100);
                WiFi.scanDelete();
                if (WiFi.status() == WL_CONNECTED) return true;
                break;
            }
        }
    }
    WiFi.scanDelete();
    return false;
}

void wifimulti_init() {
    MDNS.begin("sparkminer");           // stabiler Name, auch im AP
    Serial.println("[WIFI-MULTI] init, mDNS sparkminer.local");
    if (tryConnectNext()) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[WIFI-MULTI] connected: %s\n", WiFi.localIP().toString().c_str());
        s_apMode = false;
    } else {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID_PREFIX "C2D8", AP_PASSWORD);   // AP_SSID_PREFIX+MAC in echtem Code
        s_apMode = true;
        MDNS.addService("http", "tcp", 80);
        Serial.println("[WIFI-MULTI] Fallback AP active");
    }
    syncIp();
}

void wifimulti_loop() {
    if (s_apMode) {
        // Immer wieder prüfen, ob ein gespeichertes Netz in Reichweite ist
        if (millis() - s_lastAttempt > 30000) {
            s_lastAttempt = millis();
            if (tryConnectNext()) {
                s_apMode = false;
                WiFi.softAPdisconnect(true);
                Serial.println("[WIFI-MULTI] AP→station switch");
                syncIp();
            }
        }
        return;
    }
    if (WiFi.status() == WL_CONNECTED) return;
    // Station verloren → versuchen neu, sonst AP
    if (millis() - s_lastAttempt > 30000) {
        s_lastAttempt = millis();
        if (!tryConnectNext()) {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(AP_SSID_PREFIX "C2D8", AP_PASSWORD);
            s_apMode = true;
            Serial.println("[WIFI-MULTI] -> Fallback AP");
            syncIp();
        }
    }
}

bool wifimulti_is_connected() { return WiFi.status() == WL_CONNECTED; }
bool wifimulti_is_ap_mode() { return s_apMode; }
const char* wifimulti_get_ip() { syncIp(); return s_ip; }
void wifimulti_rescan() { s_lastAttempt = 0; }
```

- [x] **Schritt 3: `src/net/wifi_multi.cpp` als Kompilier-Einheit prüfen**

PlatformIO kompiliert `src/` rekursiv → `src/net/wifi_multi.cpp` wird automatisch gebaut.
Run: `pio run -e esp32-headless`
Expected: `[SUCCESS]` (mDNS-Teil prüfen: `#include <ESPmDNS.h>` verfügbar).

- [x] **Schritt 4: Committen**

```bash
git add src/net/wifi_multi.h src/net/wifi_multi.cpp
git commit -m "feat(wifi): multi-WLAN + Auto-Reconnect + Fallback-AP Modul"
```

---

### Task 3: `wifi_manager` durch `wifi_multi` ersetzen in `main.cpp` + `monitor.cpp`

**Files:**
- Modify: `src/main.cpp` (Include + setup-Aufruf + loop)
- Modify: `src/stats/monitor.cpp` (get_ip → wifimulti_get_ip)
- Delete (optional, Aufräumen): `src/config/wifi_manager.cpp/.h` — wird nicht mehr referenziert

**Interfaces:**
- Consumes: `wifimulti_init()`, `wifimulti_loop()`, `wifimulti_get_ip()` (Task 2).

- [x] **Schritt 1: `main.cpp` Include ersetzen**

```cpp
// vorher: #include "config/wifi_manager.h"
#include "net/wifi_multi.h"
```

- [x] **Schritt 2: `main.cpp` Setup-Aufruf ersetzen**

```cpp
// vorher: wifi_manager_init(); ...; wifi_manager_start();
wifimulti_init();
// behalte die WiFi.onEvent(...)-Diagnose-Blocke unverändert
```

- [x] **Schritt 3: `main.cpp` loop() um `wifimulti_loop()` ergänzen**

```cpp
void loop() {
    wifimulti_loop();                 // Auto-Reconnect + Fallback
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

- [x] **Schritt 4: `monitor.cpp` IP-Quelle wechseln**

```cpp
// vorher: #include "../config/wifi_manager.h"  →  #include "../net/wifi_multi.h"
// in updateDisplayData:
data->ipAddress = wifimulti_get_ip();   // statt wifi_manager_get_ip()
```

- [x] **Schritt 5: Alte Dateien entfernen**

```bash
git rm src/config/wifi_manager.cpp src/config/wifi_manager.h
```

- [x] **Schritt 6: Build prüfen**

Run: `pio run -e esp32-headless`
Expected: `[SUCCESS]` und Serial-Log enthält `[WIFI-MULTI]`.

- [x] **Schritt 7: Committen**

```bash
git add -A
git commit -m "refactor(wifi): nutze wifi_multi statt wifi_manager"
```

---

### Task 4: Dashboard WiFi-Panel + `/api/wifi` Endpoints

**Files:**
- Modify: `src/web/dashboard.cpp` (Endpoints + Handler)
- Modify: `src/web/dashboard_html.h` (Netzwerk-Panel + JS)

**Interfaces:**
- Consumes: `wifimulti_*`, `miner_config_t.wifiNetworks[]` (Task 1/2).
- Produces: Endpoints `GET /api/wifi`, `POST /api/wifi`, `DELETE /api/wifi?ssid=...`.

- [x] **Schritt 1: `handleWifiList` (GET)**

```cpp
static void handleWifiList() {
    miner_config_t *cfg = nvs_config_get();
    StaticJsonDocument<1024> doc;
    JsonArray nets = doc.createNestedArray("networks");
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++) {
        net = nets.createNestedObject();
        net["ssid"] = cfg->wifiNetworks[i].ssid;
        net["active"] = (WiFi.SSID() == cfg->wifiNetworks[i].ssid);
    }
    doc["connected"] = wifimulti_is_connected();
    doc["ap_mode"] = wifimulti_is_ap_mode();
    doc["ip"] = wifimulti_get_ip();
    char buf[512]; size_t len = serializeJson(doc, buf, sizeof(buf));
    s_server.send(200, "application/json", String(buf, len));
}
```

- [x] **Schritt 2: `handleWifiAdd` (POST)**

Body JSON: `{"ssid": "...", "password": "..."}`. Fügt hinzu (max 5, dedup), speichert NVS, ruft `wifimulti_rescan()`.

```cpp
static void handleWifiAdd() {
    StaticJsonDocument<256> body;
    deserializeJson(body, s_server.arg("plain"));
    const char* ssid = body["ssid"] | "";
    const char* pw = body["password"] | "";
    if (!ssid[0]) { s_server.send(400, "text/plain", "missing ssid"); return; }
    miner_config_t *cfg = nvs_config_get();
    // Dedup
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++)
        if (strcmp(cfg->wifiNetworks[i].ssid, ssid) == 0)
            { nvs_config_save(cfg); s_server.send(200, "text/plain", "exists"); return; }
    if (cfg->wifiNetworkCount >= MAX_WIFI_NETWORKS)
        { s_server.send(400, "text/plain", "max 5"); return; }
    wifi_network_t *wn = &cfg->wifiNetworks[cfg->wifiNetworkCount++];
    strncpy(wn->ssid, ssid, MAX_SSID_LENGTH); wn->ssid[MAX_SSID_LENGTH] = '\0';
    strncpy(wn->password, pw, MAX_PASSWORD_LEN); wn->password[MAX_PASSWORD_LEN] = '\0';
    nvs_config_save(cfg);
    wifimulti_rescan();
    s_server.send(200, "text/plain", "ok");
}
```

- [x] **Schritt 3: `handleWifiDelete` (DELETE)**

```cpp
static void handleWifiDelete() {
    const char* ssid = s_server.arg("ssid").c_str();
    miner_config_t *cfg = nvs_config_get();
    for (uint8_t i = 0; i < cfg->wifiNetworkCount; i++) {
        if (strcmp(cfg->wifiNetworks[i].ssid, ssid) == 0) {
            // verschiebe letzte einen hoch
            cfg->wifiNetworks[i] = cfg->wifiNetworks[cfg->wifiNetworkCount - 1];
            cfg->wifiNetworkCount--;
            nvs_config_save(cfg);
            wifimulti_rescan();
            s_server.send(200, "text/plain", "ok");
            return;
        }
    }
    s_server.send(404, "text/plain", "not found");
}
```

- [x] **Schritt 4: Routen registrieren**

In `dashboard_start()` nach bestehenden Routen:

```cpp
s_server.on("/api/wifi", HTTP_GET, handleWifiList);
s_server.on("/api/wifi", HTTP_POST, handleWifiAdd);
s_server.on("/api/wifi", HTTP_DELETE, handleWifiDelete);
```

Einbindung der Includes (`../config/nvs_config.h` bereits vorhanden; `../net/wifi_multi.h` hinzufügen).

- [x] **Schritt 5: HTML-Panel + JS**

In `dashboard_html.h` ein neues Card "Netzwerke" einfügen mit: Liste der gespeicherten SSIDs, Eingabefelder (SSID+Passwort) + "Hinzufügen"-Button, Löschen-Knopf je Eintrag; `fetch('/api/wifi')` beim Laden + nach Änderung. `setInterval` des Stats bleibt bei 1s (WiFi-Liste zusätzlich bei Bedarf).

- [x] **Schritt 6: Build + manueller Request-Test**

Run: `pio run -e esp32-headless` → `[SUCCESS]`.
Danach per `Invoke-WebRequest` testen (ESP am USB/Netz erreichbar):
- `GET http://<ip>/api/wifi` → 200, `{"networks":[...],"connected":true,...}`.

- [x] **Schritt 7: Committen**

```bash
git add src/web/dashboard.cpp src/web/dashboard_html.h
git commit -m "feat(dashboard): WLAN-Verwaltung + /api/wifi"
```

---

### Task 5: Dashboard im Fallback-AP erreichbar halten

**Files:**
- Modify: `src/web/dashboard.cpp` (dashboard_task: start auch im AP)

**Interfaces:**
- Consumes: `wifimulti_is_ap_mode()`, `wifimulti_is_connected()` (Task 2).
- Erwartung: Dashboard startet, sobald Station-IP ODER Fallback-AP aktiv ist.

- [x] **Schritt 1: `dashboard_task`-Wartelogik erweitern**

Aktuell wartet der Task bis zu 20 s auf `WL_CONNECTED`. Neu: Warten bis entweder Station verbunden ODER AP-Modus aktiv (beides über `wifimulti_*`), dann `dashboard_start()`.

```cpp
unsigned long waitStart = millis();
while (!wifimulti_is_connected() && !wifimulti_is_ap_mode()
       && (millis() - waitStart) < 20000) {
    vTaskDelay(pdMS_TO_TICKS(200));
}
if (wifimulti_is_ap_mode() || wifimulti_is_connected()) {
    dashboard_start();
} else {
    Serial.println("[DASHBOARD] Kein Netz/AP - Dashboard disabled");
}
```

Im AP-Modus liefert `wifimulti_get_ip()` `192.168.4.1` → `dashboard_start()`-Log stimmt.

- [x] **Schritt 2: Build prüfen**

Run: `pio run -e esp32-headless` → `[SUCCESS]`.

- [x] **Schritt 3: `wifi_multi.h` in dashboard.cpp includen + committen**

```bash
git add src/web/dashboard.cpp
git commit -m "feat(dashboard): Start auch im Fallback-AP (192.168.4.1)"
```

---

### Task 6: Verifikation + Inspektor-Veto + Flash-Zyklus

**Files:** (kein fester Pfad — Verifikationsschritte)
- Test: Build, Serial-Log, HTTP-Requests, Inspektor-Review.

- [x] **Schritt 1: Komplett-Build**

Run: `pio run -e esp32-headless`
Expected: `[SUCCESS]`, kein Mojibake/keine Warnungen bezüglich neuer Module.

- [x] **Schritt 2: Unit-aware Serial-Checks (nach Flash)**

Nach `-t upload --upload-port COM7` per `sparkmonitor.ps1`:
- `[WIFI-MULTI]` + `connected: 192.168.x.x` oder `Fallback AP active` erscheint.
- `[DASHBOARD] Web server started at http://...` erscheint.

- [x] **Schritt 3: mDNS + HTTP-Test**

An ESP-IP:
- `ping sparkminer.local` oder Browser `http://sparkminer.local` (im gleichen WLAN).
- `GET /api/wifi` → 200 JSON.

- [x] **Schritt 4: Inspektor-Veto**

Unabhängigen Agenten (general) den geänderten Code (wifi_multi, dashboard, nvs_config, main) reviewen lassen — Thread-Safety, NVS-Kompatibilität, Reichweite der AP-Fallback-Logik, JSON-Budgets. Erst nach APPROVE flashen.

- [x] **Schritt 5: Flash**

```bash
pio run -e esp32-headless -t upload --upload-port COM7
```
Confirm: Hash verified, `[SUCCESS]`. (Ursprünglich erwartet: config-Struktur-Änderung erfordert Neu-Konfiguration. **In der Umsetzung durch die NVS-Migration vermieden** — Wallet/Pool/WLAN bleiben beim Update erhalten.)

---

## Status: ABGESCHLOSSEN (2026-08-31)

Alle 6 Tasks umgesetzt, geflasht und verifiziert.

- **Commits:** `c773056` (WLAN-Array + wifi_multi), `d7c96cf` (Dashboard WiFi + AP),
  `36b08b7` (Inspektor-Fixes: NVS-Migration, Config-Race-Mutex, JSON-Budget).
- **Build:** `pio run -e esp32-headless` → `[SUCCESS]`.
- **Flash:** COM7 ohne Erase, Hash verified. NVS/Wallet/Pool erhalten (Migration griff).
- **Serial-Log:** `[WIFI-MULTI] Connected to 'GatewayToHell' (192.168.0.160)`,
  Dashboard gestartet, Mining aktiv.
- **Verifikation live:**
  - mDNS `http://sparkminer.local/api/stats` → 200 (Hashrate ~429 KH/s).
  - `/api/wifi` GET/POST/DELETE end-to-end getestet (Dummy hinzugefügt/gelistet/entfernt;
    migrierter Eintrag `GatewayToHell` bleibt aktiv).
- **Inspektor-Veto:** APPROVED (2. Review-Runde).
- **Abweichung vom Plan:** NVS-Migration ersetzt die geplante (einmalige) Neu-Konfiguration;
  Fallback-AP-SSID ist dynamisch `SparkMiner_` + MAC-Suffix statt fix `SparkMiner_C2D8`.

### Offen / Folge-Tasks
- Fallback-AP-Manuelltest (alle Netze löschen → AP `SparkMiner_*` / `192.168.4.1` prüfen).
- Nichtblockierende FSM für `wifi_multi` (derzeit ~10 s Block im loopTask; Mining/Stratum
  laufen ungestört in eigenen Tasks).
- Fernzugriff-Erreichbarkeit via Tailnet vom Notebook testen (nach User-Tailscale-Setup).
