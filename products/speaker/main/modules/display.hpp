/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool display_init(bool default_dummy_draw);

/**
 * @brief Set hungry state for feeding functionality
 * @param is_hungry true if device is hungry, false otherwise
 */
void display_set_hungry_state(bool is_hungry);

#ifdef __cplusplus
}
#endif
