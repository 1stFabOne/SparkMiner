# SparkMiner — Netzwerk-Robustheit & Fernzugriff (Design)

Datum: 2026-08-31
Projekt: SparkMiner (C:\SecondBrain\tmp\SparkMiner), ESP32-D0WD-V3, headless, Web-Dashboard live.

## Ausgangslage / Problem

- Der ESP hat ein **kaputtes Display**; die einzige Sicht sind Serial + das neu gebaute Live-Web-Dashboard.
- Der ESP hängt aktuell in einem WLAN (`192.168.0.x`), ist aber bei wechselnden Standorten /
  mitgenommen werden (Handy-Hotspot) in wechselnden Netzen unterwegs.
- Die Adresse des ESP ist eine **variable DHCP-IP** — schwer zu merken/zu finden.
- Der Zugriff von **unterwegs** (Mobilfunk) funktioniert aktuell nicht; erforderlich ist ein
  Fernzugriffsweg über einen dauerhaft laufenden Rechner im Heimnetz.

## Ziel

Ein kohärentes Netzwerk-Feature-Bündel, das dem (headless) ESP erlaubt:

1. **Stabiler Name** statt wechselnder IP (mDNS).
2. **Mehrere gespeicherte WLANs** (bis 5) mit automatischem Scan/Reconnect — nimmt das, was
   gerade verfügbar ist; inkl. Heim + Handy-Hotspot.
3. **WLAN-Verwaltung im Dashboard** (Passwörter hinzufügen/löschen).
4. **Fallback-AP mit Dashboard**: Wenn kein bekanntes WLAN verfügbar, startet der ESP ein
   eigenes WLAN (SSID `SparkMiner_` + letzte 6 Hex des MAC, Passwort `minebitcoin`), auf dem
   das Dashboard weiterläuft (`http://192.168.4.1`), damit man sich auch ohne bestehendes
   Netz neu verbinden kann. Sobald wieder ein bekanntes WLAN in Reichweite kommt, wechselt
   der ESP zurück in den Station-Modus.
5. **Fernzugriff von überall** via Tailscale, wobei der 24/7-Desktop-PC als Subnet-Router
   fungiert. (Der ESP selbst macht keinen Tunnel — zu ressourcenschwach, kein Daemon.)

## Architektur-Entscheidungen

### A. mDNS-Name
- Nutzt `ESPmDNS.h` (im ESP32-Arduino-Core enthalten).
- Hostname: `sparkminer`.
- Erreichbar unter `http://sparkminer.local` in jedem verbundenen WLAN (sofern mDNS im
  Client-Netz propagiert wird — bei Windows/Android nativ).
- Wird nach erfolgreicher WLAN-Verbindung gestartet (`MDNS.begin("sparkminer")`).

### B. WLAN-Liste (bis 5) + Auto-Reconnect
- Ersetzt das bisherige Single-`ssid`/`wifiPassword`-Modell der `miner_config_t`.
- Neue persistente Struktur: Array aus bis zu 5 Einträgen (Typ `wifi_network_t`) mit
  `ssid[MAX_SSID_LENGTH+1]` (63) und `password[MAX_PASSWORD_LEN+1]` (64), plus
  `wifiNetworkCount`.
- Logik:
  - Beim Boot & bei Verbindungsverlust: alle gespeicherten WLANs der Reihe nach versuchen,
    Verbindung zum zuerst verfügbaren aufbauen.
  - Periodischer Reconnect (z.B. alle 30 s) ohne Verbindung → erneut scannen.
  - Aktive SSID wird angezeigt; mehrere Netze bleiben persistiert.
- Kompatibilität: `WiFi.begin(ssid, pass)` pro Eintrag; kein WiFiManager-Doppel-Scan nötig,
  einfaches manuelles Scannen und Verbinden (WiFiManager bleibt nur für das Ersteinrichtungs-
  Portal bzw. wird vom neuen WLAN-Manager abgelöst).

### C. WLAN-Verwaltung im Dashboard
- Neues Dashboard-Panel "Netzwerke".
- Endpoints:
  - `GET /api/wifi` → Liste der gespeicherten WLANs + aktive SSID + Signal.
  - `POST /api/wifi` → neues WLAN hinzufügen (Body: `{ssid, password}`).
  - `DELETE /api/wifi?ssid=...` → WLAN entfernen.
- Nach Änderung: NVS speichern + Netzwerk-Scan/Reconnect anstoßen.

### D. Fallback-AP mit Dashboard
- Wenn nach vollständigem Versuch **kein** WLAN verbunden ist:
  - ESP startet eigenen AP (SSID `SparkMiner_` + MAC-Suffix, Passwort `minebitcoin`).
  - Webserver läuft **auch im AP-Modus** weiter unter `http://192.168.4.1`.
  - Dashboard + WLAN-Verwaltung sind so auch ohne bestehendes Netz erreichbar.
  - Sobald wieder ein bekanntes WLAN gefunden wird → zurück in den Station-Modus.

### F. NVS-Migration (Konfigurations-Erhalt)
- Der Umbau von Einzel-`ssid`/`wifiPassword` auf das `wifiNetworks[]`-Array ändert die
  binäre Struct-Größe in NVS. Ohne Migration würde ein Firmware-Update auf bestehenden
  Geräten die alte Config (Wallet, Pools, WLAN) verwerfen.
- `nvs_config_load` erkennt die alte Struct-Größe, liest den alten Binär-Block, verifiziert
  dessen Checksum und übernimmt `ssid`/`wifiPassword` → `wifiNetworks[0]` sowie alle übrigen
  Felder (Wallet, Pools, Display, Mining, Stats) in die neue Struktur. Anschließend wird die
  neue Struktur persistiert, sodass der nächste Boot direkt ohne Re-Migration lädt.
- Ergebnis: Kein Datenverlust beim Update; Reprovierung nur noch bei wirklich leerem NVS nötig.

### E. Fernzugriff (Tailscale) — Infrastruktur
- **ESP selbst: keine Änderung.**
- 24/7-Desktop-PC (Windows, im Heimnetz `192.168.0.x`): Tailscale installieren, als
  Subnet-Router `192.168.0.0/24` freigeben.
- Handy (Android) + Notebook: Tailscale-App, gleiches Konto.
- Ergebnis: Von überall `http://192.168.0.160` (bzw. `http://sparkminer.local` im Heimnetz).
- Einschränkung: Fernzugriff hängt am laufenden Desktop-PC.

## Komponenten

Neues Modul `src/net/wifi_multi.h/.cpp` (mehrere SSIDs, Scan/Reconnect, Fallback-AP),
ergänzt um:
- `miner_config_t`: WLAN-Array (max 5) statt Einzel-`ssid`/`wifiPassword`.
- `dashboard.cpp/.h`: WiFi-Sub-Panel + `/api/wifi`-Endpoints.
- `main.cpp`: mDNS-Init + Aufruf des neuen WLAN-Managers statt `wifi_manager_*`.

Abhängigkeiten: nur Arduino-Core (WiFi, ESPmDNS, WebServer) + ArduinoJson (vorhanden).

## Edge-Cases / Fehlerbehandlung

- **Kein bekanntes WLAN**: Fallback-AP, kein Endlos-Connect-Loop; periodischer Rescan.
- **Hotspot wechselt**: Dynamische IP — mDNS-Name ist der primäre Zugang.
- **Zwei Netze gleichzeitig sichtbar**: Priorität der gespeicherten Reihenfolge; erste
  erfolgreiche Verbindung gewinnt.
- **Passwort falsch**: Eintrag wird übersprungen, nächster Netzwerkversuch.
- **Verbindung bricht ab**: Reconnect-Zyklus (30 s), dann Fallback-AP.

## Testing / Verifikation

- Build `esp32-headless` kompiliert fehlerfrei.
- mDNS: `ping sparkminer.local` / Browser `http://sparkminer.local` lokal erreichbar.
- Multi-WLAN: Heim + Handy-Hotspot beide hinterlegt; ESP wechselt automatisch.
- Fallback-AP: ESP ohne bekanntes Netz → AP sichtbar, Dashboard auf `192.168.4.1`.
- Inspektor-Veto vor Flash (Regel 6).

### Status (2026-08-31, umgesetzt + verifiziert)

- ✅ Implementiert (Tasks 1–6), 4 Commits: `c773056`, `d7c96cf`, `36b08b7` (+ Doku).
- ✅ Bau: `pio run -e esp32-headless` → `[SUCCESS]`.
- ✅ Flash auf COM7 (ohne Erase), Hash verified, NVS/Wallet/Pool erhalten.
- ✅ Serial-Log: `[WIFI-MULTI] Connected to 'GatewayToHell' (192.168.0.160)`,
  Dashboard gestartet, Mining aktiv.
- ✅ mDNS: `http://sparkminer.local/api/stats` → 200 (131.073 H/s live).
- ✅ `/api/wifi` GET/POST/DELETE end-to-end getestet (Dummy-Netz hinzugefügt, gelistet,
  wieder entfernt; migrierter Eintrag `GatewayToHell` bleibt aktiv).
- ✅ NVS-Migration bestätigt: alte Single-SSID-Config wurde beim Boot in `wifiNetworks[0]`
  übernommen (Wallet/Pool blieben erhalten).
- ✅ Inspektor-Veto: **APPROVED** (2. Review-Runde; Blocker NVS-Migration + Config-Race
  dank Mutex behoben).

### Offen / Folge-Tasks
- Fallback-AP-Manuelltest (alle Netze löschen → AP `SparkMiner_*` / `192.168.4.1` prüfen).
- Nichtblockierende FSM für `wifi_multi` (derzeit bis ~10 s Block im loopTask; Mining/
  Stratum laufen ungestört in eigenen Tasks).
- Fernzugriff-Erreichbarkeit via Tailnet vom Notebook testen (nach User-Tailscale-Setup).
