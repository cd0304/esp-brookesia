/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port_disp.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Display"
#include "esp_lib_utils.h"
#include "display.hpp"
#include "device_info.h"
#include "status_report.h"
#include "esp_brookesia.hpp"

// 喂食功能相关常量
constexpr int  FEEDING_CLICK_COUNT_REQUIRED = 3;     // 需要连续点击3次
constexpr int  FEEDING_CLICK_TIMEOUT_MS     = 2000;  // 2秒内的点击才算连续
constexpr int  FEEDING_ANIMATION_DURATION_MS = 5000; // 喂食动画持续5秒

constexpr int  LVGL_TASK_PRIORITY        = 4;
constexpr int  LVGL_TASK_CORE_ID         = 1;
constexpr int  LVGL_TASK_STACK_SIZE      = 20 * 1024;
constexpr int  LVGL_TASK_MAX_SLEEP_MS    = 500;
constexpr int  LVGL_TASK_TIMER_PERIOD_MS = 5;
constexpr bool LVGL_TASK_STACK_CAPS_EXT  = true;
constexpr int  BRIGHTNESS_MIN            = 10;
constexpr int  BRIGHTNESS_MAX            = 100;
constexpr int  BRIGHTNESS_DEFAULT        = 100;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::services;
using namespace esp_brookesia::systems::speaker;

// 喂食功能全局变量
static struct {
    int click_count = 0;
    uint32_t last_click_time = 0;
    bool is_hungry_state = false;
    bool is_feeding_in_progress = false;
} feeding_state;

// 粪便状态全局变量
static struct {
    bool is_pooping_state = false;
} poop_state;

static bool draw_bitmap_with_lock(lv_disp_t *disp, int x_start, int y_start, int x_end, int y_end, const void *data);
static bool clear_display(lv_disp_t *disp);
extern "C" void screen_click_event_cb(lv_event_t *e);
static void handle_feeding_logic();
static void reset_feeding_click_counter();

bool display_init(bool default_dummy_draw)
{
    ESP_UTILS_LOG_TRACE_GUARD();

    static bool is_lvgl_dummy_draw = true;

    /* Initialize BSP */
    bsp_power_init(true);
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = LVGL_TASK_PRIORITY,
            .task_stack = LVGL_TASK_STACK_SIZE,
            .task_affinity = LVGL_TASK_CORE_ID,
            .task_max_sleep_ms = LVGL_TASK_MAX_SLEEP_MS,
            .task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            .timer_period_ms = LVGL_TASK_TIMER_PERIOD_MS,
        },
        .buffer_size = BSP_LCD_H_RES * 50,
        .double_buffer = true,
        .flags = {
            .buff_spiram = false,
            .default_dummy_draw = default_dummy_draw, // Avoid white screen during initialization
        },
    };
    auto disp = bsp_display_start_with_config(&cfg);
    ESP_UTILS_CHECK_NULL_RETURN(disp, false, "Start display failed");
    if (default_dummy_draw) {
        ESP_UTILS_CHECK_FALSE_RETURN(clear_display(disp), false, "Clear display failed");
        vTaskDelay(pdMS_TO_TICKS(100)); // Avoid snow screen
    }
    bsp_display_backlight_on();

    // 设置更长的长按时间，避免与快速点击冲突
    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (touch_indev) {
        lv_indev_set_long_press_time(touch_indev, 3000); // 3秒长按时间
        ESP_UTILS_LOGI("Touch long press time set to 3000ms");
    }

    // 注意：不能在这里直接注册屏幕点击事件，因为需要通过Speaker系统的DummyDrawMask
    // 屏幕点击事件将在Speaker系统初始化时注册
    ESP_UTILS_LOGI("Display initialized - screen click events will be registered by Speaker system");

    /* Configure LVGL lock and unlock */
    LvLock::registerCallbacks([](int timeout_ms) {
        if (timeout_ms < 0) {
            timeout_ms = 0;
        } else if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        ESP_UTILS_CHECK_FALSE_RETURN(bsp_display_lock(timeout_ms), false, "Lock failed");

        return true;
    }, []() {
        bsp_display_unlock();

        return true;
    });

    /* Update display brightness when NVS brightness is updated */
    auto &storage_service = StorageNVS::requestInstance();
    storage_service.connectEventSignal([&](const StorageNVS::Event & event) {
        if ((event.operation != StorageNVS::Operation::UpdateNVS) || (event.key != Manager::SETTINGS_BRIGHTNESS)) {
            return;
        }

        ESP_UTILS_LOG_TRACE_GUARD();

        StorageNVS::Value value;
        ESP_UTILS_CHECK_FALSE_EXIT(
            storage_service.getLocalParam(Manager::SETTINGS_BRIGHTNESS, value), "Get NVS brightness failed"
        );

        auto brightness = std::clamp(std::get<int>(value), BRIGHTNESS_MIN, BRIGHTNESS_MAX);
        ESP_UTILS_LOGI("Set display brightness to %d", brightness);
        ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_brightness_set(brightness), "Set display brightness failed");
    });

    /* Initialize display brightness */
    StorageNVS::Value brightness = BRIGHTNESS_DEFAULT;
    if (!storage_service.getLocalParam(Manager::SETTINGS_BRIGHTNESS, brightness)) {
        ESP_UTILS_LOGW("Brightness not found in NVS, set to default value(%d)", std::get<int>(brightness));
    }
    storage_service.setLocalParam(Manager::SETTINGS_BRIGHTNESS, brightness);

    /* Process animation player events */
    AnimPlayer::flush_ready_signal.connect(
        [ = ](int x_start, int y_start, int x_end, int y_end, const void *data, void *user_data
    ) {
        // ESP_UTILS_LOGD("Flush ready: %d, %d, %d, %d", x_start, y_start, x_end, y_end);

        if (is_lvgl_dummy_draw) {
            ESP_UTILS_CHECK_FALSE_EXIT(
                draw_bitmap_with_lock(disp, x_start, y_start, x_end, y_end, data), "Draw bitmap failed"
            );
        }

        auto player = static_cast<AnimPlayer *>(user_data);
        ESP_UTILS_CHECK_NULL_EXIT(player, "Get player failed");

        player->notifyFlushFinished();
    });
    AnimPlayer::animation_stop_signal.connect(
        [ = ](int x_start, int y_start, int x_end, int y_end, void *user_data
    ) {
        // ESP_UTILS_LOGD("Clear area: %d, %d, %d, %d", x_start, y_start, x_end, y_end);

        if (is_lvgl_dummy_draw) {
            std::vector<uint8_t> buffer((x_end - x_start) * (y_end - y_start) * 2, 0);
            ESP_UTILS_CHECK_FALSE_EXIT(
                draw_bitmap_with_lock(disp, x_start, y_start, x_end, y_end, buffer.data()), "Draw bitmap failed"
            );
        }
    });
    Display::on_dummy_draw_signal.connect([ = ](bool enable) {
        ESP_UTILS_LOGI("Dummy draw: %d", enable);

        ESP_UTILS_CHECK_ERROR_EXIT(lvgl_port_disp_take_trans_sem(disp, portMAX_DELAY), "Take trans sem failed");
        lvgl_port_disp_set_dummy_draw(disp, enable);
        lvgl_port_disp_give_trans_sem(disp, false);

        if (!enable) {
            LvLockGuard gui_guard;
            lv_obj_invalidate(lv_screen_active());
        } else {
            ESP_UTILS_CHECK_FALSE_EXIT(clear_display(disp), "Clear display failed");
        }

        is_lvgl_dummy_draw = enable;
    });

    return true;
}

static bool draw_bitmap_with_lock(lv_disp_t *disp, int x_start, int y_start, int x_end, int y_end, const void *data)
{
    // ESP_UTILS_LOG_TRACE_GUARD();

    static boost::mutex draw_mutex;

    ESP_UTILS_CHECK_NULL_RETURN(disp, false, "Invalid display");
    ESP_UTILS_CHECK_NULL_RETURN(data, false, "Invalid data");

    auto panel_handle = static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(disp));
    ESP_UTILS_CHECK_NULL_RETURN(panel_handle, false, "Get panel handle failed");

    std::lock_guard<boost::mutex> lock(draw_mutex);

    lvgl_port_disp_take_trans_sem(disp, 0);
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, data), false, "Draw bitmap failed"
    );

    // Wait for the last frame buffer to complete transmission
    ESP_UTILS_CHECK_ERROR_RETURN(lvgl_port_disp_take_trans_sem(disp, portMAX_DELAY), false, "Take trans sem failed");
    lvgl_port_disp_give_trans_sem(disp, false);

    return true;
}

static bool clear_display(lv_disp_t *disp)
{
    ESP_UTILS_LOG_TRACE_GUARD();

    std::vector<uint8_t> buffer(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, 0);
    ESP_UTILS_CHECK_FALSE_RETURN(
        draw_bitmap_with_lock(disp, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, buffer.data()), false, "Draw bitmap failed"
    );

    return true;
}

// 屏幕点击事件回调函数
extern "C" void screen_click_event_cb(lv_event_t *e)
{
    ESP_UTILS_LOG_TRACE_GUARD();
    
    uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    ESP_UTILS_LOGD("Screen clicked, current_time: %lu, last_click_time: %lu", 
                   current_time, feeding_state.last_click_time);
    
    // 检查是否在超时时间内
    if (feeding_state.last_click_time > 0 && 
        (current_time - feeding_state.last_click_time) > FEEDING_CLICK_TIMEOUT_MS) {
        ESP_UTILS_LOGD("Click timeout, resetting counter");
        reset_feeding_click_counter();
    }
    
    // 增加点击计数
    feeding_state.click_count++;
    feeding_state.last_click_time = current_time;
    
    ESP_UTILS_LOGI("Screen click %d/%d, hungry_state: %s, feeding_in_progress: %s", 
                   feeding_state.click_count, FEEDING_CLICK_COUNT_REQUIRED,
                   feeding_state.is_hungry_state ? "YES" : "NO",
                   feeding_state.is_feeding_in_progress ? "YES" : "NO");
    
    // 检查是否达到喂食条件
    if (feeding_state.click_count >= FEEDING_CLICK_COUNT_REQUIRED && 
        feeding_state.is_hungry_state && 
        !feeding_state.is_feeding_in_progress) {
        ESP_UTILS_LOGI("🍽️ Feeding conditions met! Starting feeding sequence...");
        handle_feeding_logic();
    }
}

// 处理喂食逻辑
static void handle_feeding_logic()
{
    ESP_UTILS_LOG_TRACE_GUARD();
    
    feeding_state.is_feeding_in_progress = true;
    reset_feeding_click_counter();
    
    // 获取AI_Buddy实例
    auto ai_buddy = esp_brookesia::systems::speaker::AI_Buddy::requestInstance();
    if (!ai_buddy) {
        ESP_UTILS_LOGE("Failed to get AI_Buddy instance");
        feeding_state.is_feeding_in_progress = false;
        return;
    }
    
    ESP_UTILS_LOGI("🍽️ Playing feeding animation and sound...");
    
    // 播放wandfood动画
    if (!ai_buddy->expression.setEmoji("wandfood")) {
        ESP_UTILS_LOGE("Failed to set wandfood animation");
    }
    
    // 播放喵叫声音
    ai_buddy->sendAudioEvent({esp_brookesia::systems::speaker::AI_Buddy::AudioType::Meowing});
    
    // 增加喂食计数
    increment_feeding_count();
    
    // 设置饥饿程度为刚好 (1)
    set_hunger_level(1);
    
    // 上报喂食事件
    if (status_report_is_connected()) {
        status_report_send_now();
        ESP_UTILS_LOGI("📤 Feeding event reported to cloud");
    }
    
    // 创建定时器，5秒后切换回happy动画
    static esp_timer_handle_t feeding_timer = nullptr;
    
    if (feeding_timer) {
        esp_timer_stop(feeding_timer);
        esp_timer_delete(feeding_timer);
        feeding_timer = nullptr;
    }
    
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            ESP_UTILS_LOGI("🍽️ Feeding animation timeout, switching to happy...");
            
            auto ai_buddy = esp_brookesia::systems::speaker::AI_Buddy::requestInstance();
            if (ai_buddy) {
                if (!ai_buddy->expression.setEmoji("happy")) {
                    ESP_UTILS_LOGE("Failed to set happy animation");
                }
            }
            
            feeding_state.is_feeding_in_progress = false;
            feeding_state.is_hungry_state = false; // 喂食后不再饥饿
            
            ESP_UTILS_LOGI("🍽️ Feeding sequence completed");
        },
        .arg = nullptr,
        .name = "feeding_timer"
    };
    
    if (esp_timer_create(&timer_args, &feeding_timer) == ESP_OK) {
        esp_timer_start_once(feeding_timer, FEEDING_ANIMATION_DURATION_MS * 1000); // 转换为微秒
    } else {
        ESP_UTILS_LOGE("Failed to create feeding timer");
        feeding_state.is_feeding_in_progress = false;
    }
}

// 重置点击计数器
static void reset_feeding_click_counter()
{
    feeding_state.click_count = 0;
    feeding_state.last_click_time = 0;
}

// 设置饥饿状态的公共函数
extern "C" void display_set_hungry_state(bool is_hungry)
{
    ESP_UTILS_LOGI("Setting hungry state: %s", is_hungry ? "YES" : "NO");
    feeding_state.is_hungry_state = is_hungry;
    
    if (!is_hungry) {
        // 如果不再饥饿，重置点击计数器
        reset_feeding_click_counter();
    }
}

// 设置拉粪状态的公共函数
extern "C" void display_set_pooping_state(bool is_pooping)
{
    ESP_UTILS_LOGI("💩 Setting pooping state: %s", is_pooping ? "YES" : "NO");
    poop_state.is_pooping_state = is_pooping;
}

// 获取拉粪状态的公共函数
extern "C" bool display_get_pooping_state(void)
{
    return poop_state.is_pooping_state;
}
