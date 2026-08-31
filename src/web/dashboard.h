/*
 * SparkMiner - Web Dashboard
 * Live status dashboard served over WiFi (WebServer) for headless miners.
 *
 * Endpoints:
 *   GET /          -> HTML dashboard (auto-refresh, poll /api/stats)
 *   GET /api/stats -> JSON with all live mining + network + pool stats
 *
 * Safely reads thread-safe stats from miner/stratum/live_stats and
 * computes an EMA-smoothed hashrate locally.
 *
 * GPL v3 License
 */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>

// Dashboard task configuration (overridable via build flags)
#ifndef DASHBOARD_CORE
#define DASHBOARD_CORE 0
#endif
#ifndef DASHBOARD_PRIORITY
#define DASHBOARD_PRIORITY 1
#endif
#ifndef DASHBOARD_STACK
#define DASHBOARD_STACK 10000
#endif

/**
 * Start the dashboard web server in its own FreeRTOS task.
 * Must only be called once WiFi is connected (station mode).
 * Safe to call again after a WiFi reconnect.
 */
void dashboard_start();

/**
 * FreeRTOS task entry: runs WebServer.handleClient() loop.
 */
void dashboard_task(void *param);

/**
 * True once the dashboard server has been started.
 */
bool dashboard_is_running();

#endif // DASHBOARD_H
