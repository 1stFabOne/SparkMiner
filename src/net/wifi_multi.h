/*
 * SparkMiner - Multi-Network WiFi Controller
 * Tries a list of saved networks (up to MAX_WIFI_NETWORKS), auto-reconnects,
 * starts mDNS (sparkminer.local) and falls back to a soft-AP if no known
 * network is reachable.
 *
 * Replaces the old single-SSID wifi_manager.
 */

#ifndef WIFI_MULTI_H
#define WIFI_MULTI_H

#include <Arduino.h>

/**
 * Initialize WiFi: start mDNS, try saved networks, else start fallback AP.
 * Must be called once from setup().
 */
void wifimulti_init();

/**
 * Non-blocking reconnect/fallback logic. Call periodically from loop().
 */
void wifimulti_loop();

/**
 * true if currently connected to a station network (not the fallback AP).
 */
bool wifimulti_is_connected();

/**
 * true if the fallback soft-AP is active (no known network reachable).
 */
bool wifimulti_is_ap_mode();

/**
 * Current IP as string (station IP, or 192.168.4.1 in AP mode).
 */
const char* wifimulti_get_ip();

/**
 * Force an immediate reconnect attempt on the next loop() pass.
 * Call after the network list changed.
 */
void wifimulti_rescan();

/**
 * Current connected SSID (station mode) or empty string.
 */
const char* wifimulti_get_ssid();

/**
 * Briefly lock the config used for network switching. Call around any
 * modification of the saved network list (e.g. from the web dashboard)
 * to avoid a race with the reconnect loop on the other core.
 */
void wifimulti_lock();
void wifimulti_unlock();

#endif // WIFI_MULTI_H
