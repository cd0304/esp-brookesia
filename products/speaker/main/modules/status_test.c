/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "status_test.h"
#include "device_info.h"
#include "status_report.h"
#include "esp_lib_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_random.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "StatusTest"

static TimerHandle_t g_test_timer = NULL;
static bool g_test_running = false;

// 测试定时器回调
static void test_timer_callback(TimerHandle_t xTimer)
{
    if (!g_test_running) {
        return;
    }
    
    ESP_UTILS_LOGI("Running status test...");
    
    // 模拟各种状态变化
    increment_touch_count();
    increment_feeding_count();
    add_fitness_calories(10);
    
    // 随机设置饥饿程度
    int hunger_level = esp_random() % 4;
    set_hunger_level(hunger_level);
    
    // 随机设置是否有粪便
    bool have_feces = (esp_random() % 10) == 0; // 10%概率有粪便
    set_have_feces(have_feces);
    
    // 如果检测到粪便，增加清理次数
    if (have_feces) {
        increment_cleanup_feces_count();
    }
    
    // 随机增加遛狗次数
    if ((esp_random() % 20) == 0) { // 5%概率遛狗
        increment_walking_count();
    }
    
    // 立即上报状态
    status_report_send_now();
    
    // 打印当前状态
    char* json = get_device_info_json();
    if (json) {
        ESP_UTILS_LOGI("Current device status: %s", json);
        free(json);
    }
}

// 启动状态测试
void status_test_start()
{
    if (g_test_running) {
        ESP_UTILS_LOGW("Status test already running");
        return;
    }
    
    // ESP_UTILS_LOG_TRACE_GUARD();
    
    // 创建测试定时器（每10秒执行一次）
    g_test_timer = xTimerCreate(
        "status_test_timer",
        pdMS_TO_TICKS(10000), // 10秒
        pdTRUE, // 自动重载
        NULL,
        test_timer_callback
    );
    
    if (!g_test_timer) {
        ESP_UTILS_LOGE("Failed to create test timer");
        return;
    }
    
    // 启动定时器
    if (xTimerStart(g_test_timer, 0) != pdPASS) {
        ESP_UTILS_LOGE("Failed to start test timer");
        xTimerDelete(g_test_timer, 0);
        g_test_timer = NULL;
        return;
    }
    
    g_test_running = true;
    ESP_UTILS_LOGI("Status test started");
}

// 停止状态测试
void status_test_stop()
{
    if (!g_test_running) {
        ESP_UTILS_LOGW("Status test not running");
        return;
    }
    
    // ESP_UTILS_LOG_TRACE_GUARD();
    
    if (g_test_timer) {
        xTimerStop(g_test_timer, 0);
        xTimerDelete(g_test_timer, 0);
        g_test_timer = NULL;
    }
    
    g_test_running = false;
    ESP_UTILS_LOGI("Status test stopped");
}

// 运行一次完整的状态测试
void status_test_run_once()
{
    // ESP_UTILS_LOG_TRACE_GUARD();
    
    ESP_UTILS_LOGI("Running one-time status test...");
    
    // 测试所有状态更新函数
    increment_touch_count();
    increment_faint_count();
    increment_cleanup_feces_count();
    increment_walking_count();
    increment_feeding_count();
    set_hunger_level(2);
    add_fitness_calories(50);
    set_have_feces(true);
    
    // 获取并打印状态
    char* json = get_device_info_json();
    if (json) {
        ESP_UTILS_LOGI("Device status after test: %s", json);
        free(json);
    }
    
    // 立即上报
    if (status_report_is_connected()) {
        status_report_send_now();
        ESP_UTILS_LOGI("Status reported to server");
    } else {
        ESP_UTILS_LOGW("Status report not connected");
    }
    
    ESP_UTILS_LOGI("One-time status test completed");
}

// 获取测试运行状态
bool status_test_is_running()
{
    return g_test_running;
}
