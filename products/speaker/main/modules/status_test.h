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
 * @brief Start status test (periodic testing)
 */
void status_test_start();

/**
 * @brief Stop status test
 */
void status_test_stop();

/**
 * @brief Run one-time status test
 */
void status_test_run_once();

/**
 * @brief Check if status test is running
 * @return true if running, false if not
 */
bool status_test_is_running();

#ifdef __cplusplus
}
#endif
