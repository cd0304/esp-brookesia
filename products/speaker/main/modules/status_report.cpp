/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "status_report.h"
#include "device_info.h"
#include "esp_lib_utils.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "StatusReport"

// WebSocket客户端句柄
static esp_websocket_client_handle_t g_ws_client = NULL;
static bool g_initialized = false;
static bool g_connected = false;
static char g_server_url[256] = {0};
static int g_report_interval = 30; // 默认30秒上报一次
static esp_timer_handle_t g_report_timer = NULL;

// 定时器回调函数
static void report_timer_callback(void* arg)
{
    if (g_report_interval > 0 && g_connected) {
        status_report_send_now();
    }
}

// WebSocket事件处理函数
static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_UTILS_LOGI("WebSocket connected to %s", g_server_url);
        g_connected = true;
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_UTILS_LOGI("WebSocket disconnected");
        g_connected = false;
        break;
        
    case WEBSOCKET_EVENT_DATA:
        ESP_UTILS_LOGD("WebSocket received data: %.*s", data->data_len, (char*)data->data_ptr);
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_UTILS_LOGE("WebSocket error");
        g_connected = false;
        break;
        
    default:
        break;
    }
}

// 初始化状态上报模块
bool status_report_init()
{
    if (g_initialized) {
        return true;
    }
    
    ESP_UTILS_LOG_TRACE_GUARD();
    
    // 创建上报定时器
    esp_timer_create_args_t timer_args = {
        .callback = report_timer_callback,
        .arg = NULL,
        .name = "status_report_timer"
    };
    
    if (esp_timer_create(&timer_args, &g_report_timer) != ESP_OK) {
        ESP_UTILS_LOGE("Failed to create report timer");
        return false;
    }
    
    g_initialized = true;
    ESP_UTILS_LOGI("Status report module initialized");
    return true;
}

// 启动状态上报
bool status_report_start(const char* server_url)
{
    if (!g_initialized) {
        ESP_UTILS_LOGE("Status report module not initialized");
        return false;
    }
    
    ESP_UTILS_LOG_TRACE_GUARD();
    
    if (g_ws_client) {
        esp_websocket_client_stop(g_ws_client);
        esp_websocket_client_destroy(g_ws_client);
        g_ws_client = NULL;
    }
    
    // 保存服务器URL
    strncpy(g_server_url, server_url, sizeof(g_server_url) - 1);
    
    // 配置WebSocket客户端
    esp_websocket_client_config_t ws_cfg = {
        .uri = g_server_url,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
    };
    
    g_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!g_ws_client) {
        ESP_UTILS_LOGE("Failed to create WebSocket client");
        return false;
    }
    
    // 注册事件处理器
    esp_websocket_register_events(g_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    
    // 启动WebSocket客户端
    if (esp_websocket_client_start(g_ws_client) != ESP_OK) {
        ESP_UTILS_LOGE("Failed to start WebSocket client");
        esp_websocket_client_destroy(g_ws_client);
        g_ws_client = NULL;
        return false;
    }
    
    ESP_UTILS_LOGI("Status report started, connecting to %s", server_url);
    return true;
}

// 停止状态上报
void status_report_stop()
{
    if (g_report_timer) {
        esp_timer_stop(g_report_timer);
    }
    
    if (g_ws_client) {
        esp_websocket_client_stop(g_ws_client);
        esp_websocket_client_destroy(g_ws_client);
        g_ws_client = NULL;
    }
    
    g_connected = false;
    ESP_UTILS_LOGI("Status report stopped");
}

// 立即发送设备状态
bool status_report_send_now()
{
    if (!g_initialized || !g_connected || !g_ws_client) {
        return false;
    }
    
    ESP_UTILS_LOG_TRACE_GUARD();
    
    // 获取设备信息JSON
    char* device_info_json = get_device_info_json();
    if (!device_info_json) {
        ESP_UTILS_LOGE("Failed to get device info JSON");
        return false;
    }
    
    // 创建上报消息
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_UTILS_LOGE("Failed to create JSON object");
        free(device_info_json);
        return false;
    }
    
    cJSON_AddStringToObject(root, "type", "device_status");
    cJSON_AddStringToObject(root, "device_id", get_device_id());
    
    cJSON* data_obj = cJSON_Parse(device_info_json);
    if (!data_obj) {
        ESP_UTILS_LOGE("Failed to parse device info JSON");
        cJSON_Delete(root);
        free(device_info_json);
        return false;
    }
    cJSON_AddItemReferenceToObject(root, "data", data_obj);
    
    // 生成JSON字符串
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    free(device_info_json);
    
    if (!json_str) {
        ESP_UTILS_LOGE("Failed to print JSON");
        return false;
    }
    
    // 发送数据
    int sent_len = esp_websocket_client_send_text(g_ws_client, json_str, strlen(json_str), portMAX_DELAY);
    free(json_str);
    
    if (sent_len < 0) {
        ESP_UTILS_LOGE("Failed to send status report");
        return false;
    }
    
    // 上报成功后重置增量数据
    reset_delta_data();
    
    ESP_UTILS_LOGI("Status report sent successfully");
    return true;
}

// 设置上报间隔
void status_report_set_interval(int interval_seconds)
{
    g_report_interval = interval_seconds;
    
    if (g_report_timer) {
        esp_timer_stop(g_report_timer);
        
        if (interval_seconds > 0) {
            esp_timer_start_periodic(g_report_timer, interval_seconds * 1000000);
            ESP_UTILS_LOGI("Status report interval set to %d seconds", interval_seconds);
        }
    }
}

// 检查连接状态
bool status_report_is_connected()
{
    return g_connected;
}
