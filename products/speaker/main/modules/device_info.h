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
 * @brief Initialize device info module
 * @return true if success, false if failed
 */
bool device_info_init();

/**
 * @brief Get device info as JSON string
 * @return JSON string (must be freed by caller), NULL if failed
 */
char* get_device_info_json();

/**
 * @brief Get device ID
 * @return Device ID string
 */
const char* get_device_id();

/**
 * @brief Increment touch count
 */
void increment_touch_count();

/**
 * @brief Increment faint count
 */
void increment_faint_count();

/**
 * @brief Increment cleanup feces count
 */
void increment_cleanup_feces_count();

/**
 * @brief Increment walking count
 */
void increment_walking_count();

/**
 * @brief Increment feeding count
 */
void increment_feeding_count();

/**
 * @brief Set hunger level (0-3)
 * @param level Hunger level (0: very full, 1: just right, 2: a bit hungry, 3: starving)
 */
void set_hunger_level(int level);

/**
 * @brief Add fitness calories
 * @param calories Calories to add (can be negative)
 */
void add_fitness_calories(int calories);

/**
 * @brief Set have feces status
 * @param have_feces Whether device has feces
 */
void set_have_feces(bool have_feces);

/**
 * @brief Test function for device info (for testing purposes)
 */
void test_device_info_functions();

/**
 * @brief Test function to manually trigger get_device_info (for testing result submission)
 */
void test_get_device_info_result();

#ifdef __cplusplus
}
#endif
