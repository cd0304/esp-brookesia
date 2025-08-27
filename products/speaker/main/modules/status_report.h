/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <stdbool.h>
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize status report module
 * @return true if success, false if failed
 */
bool status_report_init();

/**
 * @brief Start status reporting
 * @param server_url WebSocket server URL
 * @return true if success, false if failed
 */
bool status_report_start(const char* server_url);

/**
 * @brief Stop status reporting
 */
void status_report_stop();

/**
 * @brief Send device status immediately
 * @return true if success, false if failed
 */
bool status_report_send_now();

/**
 * @brief Set reporting interval (in seconds)
 * @param interval_seconds Reporting interval in seconds (0 to disable auto reporting)
 */
void status_report_set_interval(int interval_seconds);

/**
 * @brief Get current connection status
 * @return true if connected, false if disconnected
 */
bool status_report_is_connected();

#ifdef __cplusplus
}
#endif
