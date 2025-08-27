/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "device_info.h"
#include "esp_lib_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_random.h"
#include <string.h>

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "DeviceInfo"

// 设备状态结构体
typedef struct {
    uint32_t continue_time;      // 设备开机累计运行时长（秒）
    uint32_t touch_num;          // 设备被抚摸的累计次数
    uint32_t faint_num;          // 设备累计晕倒次数
    bool is_have_feces;          // 设备当前是否有排泄物
    uint32_t cleanup_feces_num;  // 清理粪便的次数
    uint32_t walking_num;        // 遛宠物的次数
    uint32_t feeding_num;        // 喂食次数
    uint8_t hunger_level;        // 饥饿程度 0-3
    uint32_t fitness_calories;   // 健身卡路里数（千卡）
    char device_id[64];          // 设备ID
    uint64_t start_time;         // 设备启动时间戳
} device_status_t;

static device_status_t g_device_status = {0};
static bool g_initialized = false;
static esp_timer_handle_t g_continue_time_timer = NULL;

// 继续时间定时器回调
static void continue_time_timer_callback(void* arg)
{
    g_device_status.continue_time++;
}

// 初始化设备信息模块
bool device_info_init()
{
    if (g_initialized) {
        return true;
    }
    
    ESP_UTILS_LOG_TRACE_GUARD();
    
    // 初始化设备状态
    memset(&g_device_status, 0, sizeof(g_device_status));
    
    // 生成设备ID
    snprintf(g_device_status.device_id, sizeof(g_device_status.device_id), "esp32s3_pet_%08lx", (unsigned long)esp_random());
    
    // 设置启动时间
    g_device_status.start_time = esp_timer_get_time() / 1000000; // 转换为秒
    
    // 创建继续时间定时器
    esp_timer_create_args_t timer_args = {
        .callback = continue_time_timer_callback,
        .arg = NULL,
        .name = "continue_time_timer"
    };
    
    if (esp_timer_create(&timer_args, &g_continue_time_timer) != ESP_OK) {
        ESP_UTILS_LOGE("Failed to create continue time timer");
        return false;
    }
    
    // 启动定时器，每秒触发一次
    if (esp_timer_start_periodic(g_continue_time_timer, 1000000) != ESP_OK) {
        ESP_UTILS_LOGE("Failed to start continue time timer");
        return false;
    }
    
    g_initialized = true;
    ESP_UTILS_LOGI("Device info module initialized");
    return true;
}

// 获取设备状态JSON字符串
char* get_device_info_json()
{
    if (!g_initialized) {
        ESP_UTILS_LOGE("Device info module not initialized");
        return NULL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_UTILS_LOGE("Failed to create JSON object");
        return NULL;
    }
    
    cJSON_AddStringToObject(root, "device_id", g_device_status.device_id);
    cJSON_AddNumberToObject(root, "continue_time", g_device_status.continue_time);
    cJSON_AddNumberToObject(root, "touch_num", g_device_status.touch_num);
    cJSON_AddNumberToObject(root, "faint_num", g_device_status.faint_num);
    cJSON_AddBoolToObject(root, "is_have_feces", g_device_status.is_have_feces);
    cJSON_AddNumberToObject(root, "cleanup_feces_num", g_device_status.cleanup_feces_num);
    cJSON_AddNumberToObject(root, "walking_num", g_device_status.walking_num);
    cJSON_AddNumberToObject(root, "feeding_num", g_device_status.feeding_num);
    cJSON_AddNumberToObject(root, "hunger_level", g_device_status.hunger_level);
    cJSON_AddNumberToObject(root, "fitness_calories", g_device_status.fitness_calories);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_str;
}

// 获取设备ID
const char* get_device_id()
{
    return g_device_status.device_id;
}

// 增加抚摸次数
void increment_touch_count()
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.touch_num++;
    ESP_UTILS_LOGI("Touch count incremented to %d", g_device_status.touch_num);
}

// 增加晕倒次数
void increment_faint_count()
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.faint_num++;
    ESP_UTILS_LOGI("Faint count incremented to %d", g_device_status.faint_num);
}

// 增加清理粪便次数
void increment_cleanup_feces_count()
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.cleanup_feces_num++;
    g_device_status.is_have_feces = false; // 清理后没有粪便
    ESP_UTILS_LOGI("Cleanup feces count incremented to %d", g_device_status.cleanup_feces_num);
}

// 增加遛狗次数
void increment_walking_count()
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.walking_num++;
    ESP_UTILS_LOGI("Walking count incremented to %d", g_device_status.walking_num);
}

// 增加喂食次数
void increment_feeding_count()
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.feeding_num++;
    // 喂食后饥饿程度降低
    if (g_device_status.hunger_level > 0) {
        g_device_status.hunger_level--;
    }
    ESP_UTILS_LOGI("Feeding count incremented to %d, hunger level: %d", g_device_status.feeding_num, g_device_status.hunger_level);
}

// 设置饥饿程度
void set_hunger_level(int level)
{
    if (!g_initialized || level < 0 || level > 3) {
        return;
    }
    
    g_device_status.hunger_level = level;
    ESP_UTILS_LOGI("Hunger level set to %d", g_device_status.hunger_level);
}

// 增加健身卡路里
void add_fitness_calories(int calories)
{
    if (!g_initialized) {
        return;
    }
    
    if (calories > 0) {
        g_device_status.fitness_calories += calories;
    }
    ESP_UTILS_LOGI("Fitness calories updated to %d", g_device_status.fitness_calories);
}

// 设置是否有粪便
void set_have_feces(bool have_feces)
{
    if (!g_initialized) {
        return;
    }
    
    g_device_status.is_have_feces = have_feces;
    ESP_UTILS_LOGI("Have feces set to %s", have_feces ? "true" : "false");
}

// 测试函数
void test_device_info_functions()
{
    ESP_UTILS_LOGI("Testing device info functions...");
    
    increment_touch_count();
    increment_faint_count();
    increment_cleanup_feces_count();
    increment_walking_count();
    increment_feeding_count();
    set_hunger_level(2);
    add_fitness_calories(100);
    set_have_feces(true);
    
    char* json = get_device_info_json();
    if (json) {
        ESP_UTILS_LOGI("Device info JSON: %s", json);
        free(json);
    }
}

// 测试获取设备信息结果
void test_get_device_info_result()
{
    char* json = get_device_info_json();
    if (json) {
        ESP_UTILS_LOGI("Current device info: %s", json);
        free(json);
    } else {
        ESP_UTILS_LOGE("Failed to get device info JSON");
    }
}
