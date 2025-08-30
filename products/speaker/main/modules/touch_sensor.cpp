/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lib_utils.h"
#include "bsp/esp-bsp.h"
#include "touch_button.h"
#include "iot_button.h"
#include "touch_sensor.h"
#include "touch_sensor_lowlevel.h"
extern "C" { // temporary solution for eliminate compilation errors
#include "touch_slider_sensor.h"
}
#include "esp_brookesia_app_settings.hpp"
#include "device_info.h"
#include "status_report.h"
extern "C" {
#include "agent/audio_processor.h"
}

const static char *TAG = "Touch Sensor";

#define TOUCH_SLIDER_ENABLED 1 // Enable touch slider for petting gestures

static const uint32_t touch_channel_list[] = { // define touch channels
#ifdef BSP_TOUCH_PAD1
    BSP_TOUCH_PAD1,
#endif
#ifdef BSP_TOUCH_PAD2
    BSP_TOUCH_PAD2,
#endif
};

// Touch button handles for multi-tap gestures
static button_handle_t touch_btn_handle[2] = {NULL, NULL};

static esp_err_t init_touch_button(void)
{
    touch_lowlevel_type_t channel_type[] = {TOUCH_LOWLEVEL_TYPE_TOUCH, TOUCH_LOWLEVEL_TYPE_TOUCH};
    uint32_t channel_num = sizeof(touch_channel_list) / sizeof(touch_channel_list[0]);
    ESP_LOGI(TAG, "touch channel num: %ld\n", channel_num);
    touch_lowlevel_config_t low_config = {
        .channel_num = channel_num,
        .channel_list = (uint32_t *)touch_channel_list,
        .channel_type = channel_type,
    };

    esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create touch sensor lowlevel");

    // Touch button configuration (shared by both buttons)
    const button_config_t btn_cfg = {
        .long_press_time = 1500,  // Long press time in ms
        .short_press_time = 245,  // Short press time in ms
    };

    for (size_t i = 0; i < channel_num; i++) {
        button_touch_config_t touch_cfg_1 = {
            .touch_channel = (int32_t)touch_channel_list[i],
            .channel_threshold = 0.05, // Touch threshold (adjust as needed)
            .skip_lowlevel_init = true,
        };
        ESP_LOGI(TAG, "Touch button %d channel: %d", i + 1, touch_channel_list[i]);
        // Create first touch button device
        ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg_1, &touch_btn_handle[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create touch button 1 device: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    touch_sensor_lowlevel_start();
    ESP_LOGI(TAG, "touch button initialized");
    return ESP_OK;
}


#if TOUCH_SLIDER_ENABLED
// Touch gesture coordination variables for petting detection
static bool is_sliding_detected = false;           // True when sliding is detected
static touch_slider_handle_t touch_slider_handle = NULL;
static int petting_count = 0;                      // Count of petting gestures

static void touch_slider_callback(touch_slider_handle_t handle, touch_slider_event_t event, int32_t data, void *cb_arg)
{
    const char *TAG = "TOUCH_PETTING";
    uint32_t current_time = esp_timer_get_time() / 1000;

    switch (event) {
    // Monitor POSITION events and treat continuous movement as petting
    case TOUCH_SLIDER_EVENT_POSITION:
        ESP_LOGD(TAG, "Position event: %d (position tracking)", data);
        // Just track position changes, don't trigger actions here to avoid callback overload
        break;

    case TOUCH_SLIDER_EVENT_RIGHT_SWIPE:
        // Swipe events indicate definite petting
        if (!is_sliding_detected) {
            is_sliding_detected = true;
            ESP_LOGI(TAG, "Petting gesture detected, taking control from buttons");
        }

        ESP_LOGI(TAG, "Right swipe - Petting detected");
        petting_count++;
        
        // 增加触摸计数（抚摸事件）
        increment_touch_count();
        
        // 播放喵叫声音
        ESP_LOGI(TAG, "Playing meowing sound for petting gesture");
        // 这里需要通过全局方式访问AI_Buddy，或者通过回调函数
        // 暂时使用直接播放音频文件的方式
        audio_prompt_play_with_block("file://spiffs/meowing.mp3", 3000);
        
        break;

    case TOUCH_SLIDER_EVENT_LEFT_SWIPE:
        // Swipe events indicate definite petting
        if (!is_sliding_detected) {
            is_sliding_detected = true;
            ESP_LOGI(TAG, "Petting gesture detected, taking control from buttons");
        }

        ESP_LOGI(TAG, "Left swipe - Petting detected");
        petting_count++;
        
        // 增加触摸计数（抚摸事件）
        increment_touch_count();
        
        // 播放喵叫声音
        ESP_LOGI(TAG, "Playing meowing sound for petting gesture");
        audio_prompt_play_with_block("file://spiffs/cat-in-heat_1.mp3", 3000);
        
        break;

    case TOUCH_SLIDER_EVENT_RELEASE:
        ESP_LOGI(TAG, "Touch released, sliding_detected: %s, petting_count: %d",
                 is_sliding_detected ? "YES" : "NO", petting_count);

        // If petting was detected, report the event
        if (is_sliding_detected && petting_count > 0) {
            ESP_LOGI(TAG, "Petting session completed with %d gestures", petting_count);
            
            // 上报抚摸事件到云端
            if (status_report_is_connected()) {
                status_report_send_now();
                ESP_LOGI(TAG, "📤 Immediate status report sent after petting session");
            }
        } else {
            // No petting detected - let button system handle this as a tap
            ESP_LOGI(TAG, "No petting detected, button system will handle this touch");
        }

        // Reset gesture state
        is_sliding_detected = false;
        petting_count = 0;
        break;

    default:
        break;
    }
}

// Task function to handle touch slider events
static void touch_slider_task(void *pvParameters)
{
    touch_slider_handle_t handle = (touch_slider_handle_t)pvParameters;
    ESP_LOGI(TAG, "Touch volume control task started");
    while (1) {
        if (touch_slider_sensor_handle_events(handle) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to handle touch slider events");
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 20ms polling interval
    }
}

// Initialize touch volume control
static esp_err_t init_touch_slider(void)
{
    // Touch slider configuration - stable parameters for petting detection
    float threshold[] = {0.015f, 0.015f};  // Moderate thresholds for stable detection
    uint32_t channel_num = sizeof(touch_channel_list) / sizeof(touch_channel_list[0]);

    // Configure touch slider for petting gesture detection
    touch_slider_config_t config = {
        .channel_num = channel_num,
        .channel_list = touch_channel_list,
        .channel_threshold = threshold,
        .channel_gold_value = NULL,
        .debounce_times = 2,            // Stable debounce for reliable operation
        .filter_reset_times = 3,        // Adequate reset time for stability
        .position_range = 100,          // Position range for petting detection
        .calculate_window = 2,          // Stable calculation window
        .swipe_threshold = 3.0f,       // Moderate threshold for reliable detection
        .swipe_hysterisis = 1.5f,      // Balanced hysteresis for stability
        .swipe_alpha = 0.4f,            // Balanced responsiveness
        .skip_lowlevel_init = true,     // Use existing lowlevel init from touch buttons
    };

    // Create touch slider sensor
    esp_err_t ret = touch_slider_sensor_create(&config, &touch_slider_handle, touch_slider_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch slider sensor: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create task to handle touch events
    BaseType_t task_ret = xTaskCreate(touch_slider_task, "touchslider_task", 4096, touch_slider_handle, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch volume task");
        touch_slider_sensor_delete(touch_slider_handle);
        touch_slider_handle = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Touch slider initialized successfully");
    return ESP_OK;
}

#endif

TouchSensor::TouchSensor()
{

}

TouchSensor::~TouchSensor()
{

}

bool TouchSensor::init()
{
    ESP_UTILS_CHECK_FALSE_RETURN(ESP_OK == init_touch_button(), false, "Failed to init touch button");
#if TOUCH_SLIDER_ENABLED
    ESP_UTILS_CHECK_FALSE_RETURN(ESP_OK == init_touch_slider(), false, "Failed to init touch slider");
#endif
    return true;
}

button_handle_t TouchSensor::get_button_handle()
{
    return touch_btn_handle[0];
}
