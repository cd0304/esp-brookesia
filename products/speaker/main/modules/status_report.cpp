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
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include "esp_brookesia_speaker_ai_buddy.hpp"
#include "agent/audio_processor.h"
#include "agent/esp_brookesia_ai_agent.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "StatusReport"

using namespace esp_brookesia::systems::speaker;
using namespace esp_brookesia::ai_framework;

// WebSocket客户端句柄
static esp_websocket_client_handle_t g_ws_client = NULL;
static bool g_initialized = false;
static bool g_connected = false;
static char g_server_url[256] = {0};
static int g_report_interval = 30; // 默认30秒上报一次
static esp_timer_handle_t g_report_timer = NULL;

// 发送命令执行结果响应
static void send_command_response(const char* command, bool success, const char* message)
{
    if (!g_connected || !g_ws_client) {
        return;
    }
    
    cJSON *response = cJSON_CreateObject();
    if (!response) {
        ESP_UTILS_LOGE("Failed to create response JSON object");
        return;
    }
    
    cJSON_AddStringToObject(response, "type", "command_response");
    cJSON_AddStringToObject(response, "command", command);
    cJSON_AddBoolToObject(response, "success", success);
    cJSON_AddStringToObject(response, "message", message);
    cJSON_AddStringToObject(response, "device_id", get_device_id());
    
    char* response_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    if (response_str) {
        esp_websocket_client_send_text(g_ws_client, response_str, strlen(response_str), portMAX_DELAY);
        ESP_UTILS_LOGI("Command response sent: %s", response_str);
        free(response_str);
    }
}

// 处理WebSocket接收到的命令
static void handle_websocket_command(char* data, int data_len)
{
    // 确保数据以null结尾
    char* json_str = (char*)malloc(data_len + 1);
    if (!json_str) {
        ESP_UTILS_LOGE("Failed to allocate memory for command data");
        return;
    }
    
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';
    
    cJSON *json = cJSON_Parse(json_str);
    free(json_str);
    
    if (!json) {
        ESP_UTILS_LOGE("Failed to parse command JSON");
        return;
    }
    
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    cJSON *command_item = cJSON_GetObjectItem(json, "command");
    
    if (!type_item || !command_item) {
        ESP_UTILS_LOGE("Invalid command format: missing type or command");
        cJSON_Delete(json);
        return;
    }
    
    const char* type = cJSON_GetStringValue(type_item);
    const char* command = cJSON_GetStringValue(command_item);
    
    if (!type || !command) {
        ESP_UTILS_LOGE("Invalid command format: type or command is not string");
        cJSON_Delete(json);
        return;
    }
    
    // 只处理command类型的消息
    if (strcmp(type, "command") != 0) {
        ESP_UTILS_LOGD("Ignoring non-command message type: %s", type);
        cJSON_Delete(json);
        return;
    }
    
    ESP_UTILS_LOGI("Received command: %s", command);
    
    // 处理具体命令
    if (strcmp(command, "generate_feces") == 0) {
        // 生成排泄物命令
        set_have_feces(true);
        send_command_response(command, true, "Feces generated successfully");
        ESP_UTILS_LOGI("✅ Command executed: generate_feces");
        
        // 立即上报状态变化
        status_report_send_now();
        ESP_UTILS_LOGI("📤 Immediate status report sent after generate_feces");
        
    } else if (strcmp(command, "set_hunger_level") == 0) {
        // 设置饥饿程度命令
        cJSON *level_item = cJSON_GetObjectItem(json, "level");
        if (!level_item || !cJSON_IsNumber(level_item)) {
            send_command_response(command, false, "Missing or invalid level parameter");
            ESP_UTILS_LOGE("❌ Command failed: set_hunger_level - missing level parameter");
        } else {
            int level = cJSON_GetNumberValue(level_item);
            if (level < 0 || level > 3) {
                send_command_response(command, false, "Level parameter must be 0-3");
                ESP_UTILS_LOGE("❌ Command failed: set_hunger_level - invalid level: %d", level);
            } else {
                set_hunger_level(level);
                char success_msg[64];
                snprintf(success_msg, sizeof(success_msg), "Hunger level set to %d successfully", level);
                send_command_response(command, true, success_msg);
                ESP_UTILS_LOGI("✅ Command executed: set_hunger_level to %d", level);
                
                // 立即上报状态变化
                status_report_send_now();
                ESP_UTILS_LOGI("📤 Immediate status report sent after set_hunger_level");
            }
        }
        
    } else if (strcmp(command, "set_expression") == 0) {
        // 设置表情命令
        cJSON *expression_item = cJSON_GetObjectItem(json, "expression");
        if (!expression_item || !cJSON_IsString(expression_item)) {
            send_command_response(command, false, "Missing or invalid expression parameter");
            ESP_UTILS_LOGE("❌ Command failed: set_expression - missing expression parameter");
        } else {
            const char* expression_name = cJSON_GetStringValue(expression_item);
            
            // 获取AI_Buddy实例
            auto ai_buddy = AI_Buddy::requestInstance();
            if (!ai_buddy) {
                send_command_response(command, false, "AI_Buddy instance not available");
                ESP_UTILS_LOGE("❌ Command failed: set_expression - AI_Buddy not available");
            } else {
                // 检查是否为临时表情（可选的duration参数）
                cJSON *duration_item = cJSON_GetObjectItem(json, "duration");
                bool success = false;
                
                if (duration_item && cJSON_IsNumber(duration_item)) {
                    // 临时表情，有持续时间
                    int duration_ms = cJSON_GetNumberValue(duration_item);
                    if (duration_ms <= 0 || duration_ms > 60000) { // 限制在1分钟内
                        send_command_response(command, false, "Duration must be between 1-60000ms");
                        ESP_UTILS_LOGE("❌ Command failed: set_expression - invalid duration: %d", duration_ms);
                    } else {
                        success = ai_buddy->expression.insertEmojiTemporary(expression_name, duration_ms);
                        if (success) {
                            char success_msg[256];
                            snprintf(success_msg, sizeof(success_msg), "Expression '%s' set temporarily for %dms", expression_name, duration_ms);
                            send_command_response(command, true, success_msg);
                            ESP_UTILS_LOGI("✅ Command executed: set_expression '%s' for %dms", expression_name, duration_ms);
                        }
                    }
                } else {
                    // 持续表情，无持续时间
                    success = ai_buddy->expression.setEmoji(expression_name);
                    if (success) {
                        char success_msg[256];
                        snprintf(success_msg, sizeof(success_msg), "Expression '%s' set successfully", expression_name);
                        send_command_response(command, true, success_msg);
                        ESP_UTILS_LOGI("✅ Command executed: set_expression '%s'", expression_name);
                    }
                }
                
                if (!success) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Failed to set expression '%s' (invalid expression name?)", expression_name);
                    send_command_response(command, false, error_msg);
                    ESP_UTILS_LOGE("❌ Command failed: set_expression '%s'", expression_name);
                }
            }
        }
        
    } else if (strcmp(command, "play_sound") == 0) {
        // 播放声音命令
        cJSON *sound_item = cJSON_GetObjectItem(json, "sound");
        if (!sound_item || !cJSON_IsString(sound_item)) {
            send_command_response(command, false, "Missing or invalid sound parameter");
            ESP_UTILS_LOGE("❌ Command failed: play_sound - missing sound parameter");
        } else {
            const char* sound_name = cJSON_GetStringValue(sound_item);
            
            // 获取AI_Buddy实例
            auto ai_buddy = AI_Buddy::requestInstance();
            if (!ai_buddy) {
                send_command_response(command, false, "AI_Buddy instance not available");
                ESP_UTILS_LOGE("❌ Command failed: play_sound - AI_Buddy not available");
            } else {
                bool success = false;
                char success_msg[512] = {0};
                char error_msg[512] = {0};
                
                // 创建系统音频类型映射表
                static const struct {
                    const char* name;
                    AI_Buddy::AudioType type;
                } audio_type_map[] = {
                    {"wifi_need_connect", AI_Buddy::AudioType::WifiNeedConnect},
                    {"wifi_connected", AI_Buddy::AudioType::WifiConnected},
                    {"wifi_disconnected", AI_Buddy::AudioType::WifiDisconnected},
                    {"server_connected", AI_Buddy::AudioType::ServerConnected},
                    {"server_disconnected", AI_Buddy::AudioType::ServerDisconnected},
                    {"server_connecting", AI_Buddy::AudioType::ServerConnecting},
                    {"mic_on", AI_Buddy::AudioType::MicOn},
                    {"mic_off", AI_Buddy::AudioType::MicOff},
                    {"wake_up", AI_Buddy::AudioType::WakeUp},
                    {"response_lai_lo", AI_Buddy::AudioType::ResponseLaiLo},
                    {"response_wo_zai_ting_ne", AI_Buddy::AudioType::ResponseWoZaiTingNe},
                    {"response_wo_zai", AI_Buddy::AudioType::ResponseWoZai},
                    {"response_zai_ne", AI_Buddy::AudioType::ResponseZaiNe},
                    {"sleep_bai_bai_lo", AI_Buddy::AudioType::SleepBaiBaiLo},
                    {"sleep_hao_de", AI_Buddy::AudioType::SleepHaoDe},
                    {"sleep_wo_tui_xia_le", AI_Buddy::AudioType::SleepWoTuiXiaLe},
                    {"sleep_xian_zhe_yang_lo", AI_Buddy::AudioType::SleepXianZheYangLo},
                    {"invalid_config", AI_Buddy::AudioType::InvalidConfig},
                    {"coze_error_insufficient_credits", AI_Buddy::AudioType::CozeErrorInsufficientCreditsBalance},
                    {"meowing", AI_Buddy::AudioType::Meowing},
                };
                
                // 首先尝试匹配系统音频类型
                bool found_system_audio = false;
                for (const auto& mapping : audio_type_map) {
                    if (strcmp(sound_name, mapping.name) == 0) {
                        // 获取可选的repeat参数
                        cJSON *repeat_item = cJSON_GetObjectItem(json, "repeat");
                        int repeat_count = 1; // 默认播放1次
                        if (repeat_item && cJSON_IsNumber(repeat_item)) {
                            repeat_count = cJSON_GetNumberValue(repeat_item);
                            if (repeat_count < 1 || repeat_count > 10) {
                                repeat_count = 1; // 限制重复次数
                            }
                        }
                        
                        // 发送音频事件
                        ai_buddy->sendAudioEvent({mapping.type, repeat_count, 0});
                        success = true;
                        found_system_audio = true;
                        snprintf(success_msg, sizeof(success_msg), "System audio '%s' played successfully (repeat: %d)", sound_name, repeat_count);
                        ESP_UTILS_LOGI("✅ Command executed: play_sound '%s' (system audio, repeat: %d)", sound_name, repeat_count);
                        break;
                    }
                }
                
                // 如果不是系统音频，尝试直接播放文件
                if (!found_system_audio) {
                    char file_path[256];
                    // 支持完整路径或简单文件名
                    if (strstr(sound_name, "file://") != NULL) {
                        // 已经是完整路径
                        strncpy(file_path, sound_name, sizeof(file_path) - 1);
                    } else if (strstr(sound_name, ".mp3") != NULL) {
                        // 包含.mp3扩展名，添加spiffs路径
                        snprintf(file_path, sizeof(file_path), "file://spiffs/%s", sound_name);
                    } else {
                        // 不包含扩展名，添加.mp3和spiffs路径
                        snprintf(file_path, sizeof(file_path), "file://spiffs/%s.mp3", sound_name);
                    }
                    file_path[sizeof(file_path) - 1] = '\0';
                    
                    // 获取可选的timeout参数
                    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout");
                    int timeout_ms = -1; // 默认等待播放完成
                    if (timeout_item && cJSON_IsNumber(timeout_item)) {
                        timeout_ms = cJSON_GetNumberValue(timeout_item);
                        if (timeout_ms < -1 || timeout_ms > 30000) {
                            timeout_ms = -1; // 限制超时时间
                        }
                    }
                    
                    // 尝试播放文件
                    esp_err_t result = audio_prompt_play_with_block(file_path, timeout_ms);
                    if (result == ESP_OK) {
                        success = true;
                        snprintf(success_msg, sizeof(success_msg), "Audio file '%s' played successfully", file_path);
                        ESP_UTILS_LOGI("✅ Command executed: play_sound '%s' (file audio)", file_path);
                    } else {
                        snprintf(error_msg, sizeof(error_msg), "Failed to play audio file '%s' (error: %s)", file_path, esp_err_to_name(result));
                        ESP_UTILS_LOGE("❌ Command failed: play_sound '%s' - %s", file_path, esp_err_to_name(result));
                    }
                }
                
                // 发送响应
                if (success) {
                    send_command_response(command, true, success_msg);
                } else {
                    send_command_response(command, false, strlen(error_msg) > 0 ? error_msg : "Unknown audio playback error");
                }
            }
        }
        
    } else if (strcmp(command, "start_chat") == 0) {
        // 启动AI对话命令
        auto ai_buddy = AI_Buddy::requestInstance();
        auto _agent = Agent::requestInstance();
        
        if (!ai_buddy || !_agent) {
            send_command_response(command, false, "AI_Buddy or Agent instance not available");
            ESP_UTILS_LOGE("❌ Command failed: start_chat - AI_Buddy or Agent not available");
        } else if (ai_buddy->isPause()) {
            send_command_response(command, false, "AI_Buddy is paused");
            ESP_UTILS_LOGE("❌ Command failed: start_chat - AI_Buddy is paused");
        } else {
            bool success = false;
            char success_msg[256] = {0};
            
            // 获取可选的text参数
            cJSON *text_item = cJSON_GetObjectItem(json, "text");
            const char* text_content = NULL;
            if (text_item && cJSON_IsString(text_item)) {
                text_content = cJSON_GetStringValue(text_item);
                ESP_UTILS_LOGI("📝 Text content to send: %s", text_content);
            }
            
            // 检查AI对话状态
            if (_agent->hasChatState(Agent::ChatState::ChatStateStarted)) {
                if (_agent->isChatState(Agent::ChatState::ChatStateSlept)) {
                    // 如果AI处于睡眠状态，唤醒它
                    ESP_UTILS_LOGI("🤖 AI is sleeping, waking up...");
                    audio_gmf_trigger_wakeup();
                    success = true;
                    snprintf(success_msg, sizeof(success_msg), "AI chat woken up successfully");
                    ESP_UTILS_LOGI("✅ Command executed: start_chat - AI woken up");
                } else if (ai_buddy->isSpeaking()) {
                    // 如果AI正在说话，中断并重新开始
                    ESP_UTILS_LOGI("🤖 AI is speaking, interrupting and restarting...");
                    coze_chat_response_signal();
                    coze_chat_app_interrupt();
                    success = true;
                    snprintf(success_msg, sizeof(success_msg), "AI chat interrupted and restarted successfully");
                    ESP_UTILS_LOGI("✅ Command executed: start_chat - AI interrupted and restarted");
                } else {
                    // AI已经处于活跃状态
                    ESP_UTILS_LOGI("🤖 AI is already active, triggering response...");
                    coze_chat_response_signal();
                    success = true;
                    snprintf(success_msg, sizeof(success_msg), "AI chat response triggered successfully");
                    ESP_UTILS_LOGI("✅ Command executed: start_chat - AI response triggered");
                }
            } else {
                // AI对话未启动，尝试启动
                ESP_UTILS_LOGI("🤖 AI chat not started, attempting to start...");
                // 这里可以添加启动AI对话的逻辑
                // 由于没有直接的启动函数，我们触发响应信号来激活
                coze_chat_response_signal();
                success = true;
                snprintf(success_msg, sizeof(success_msg), "AI chat start attempted (may need initialization)");
                ESP_UTILS_LOGI("✅ Command executed: start_chat - AI start attempted");
            }
            
            // 如果有text参数，在启动AI对话后发送文字内容
            if (success && text_content && strlen(text_content) > 0) {
                ESP_UTILS_LOGI("📤 Sending text content to AI: %s", text_content);
                
                // 延迟更长时间确保AI对话和WebSocket连接已经完全建立
                ESP_UTILS_LOGI("⏳ Waiting for AI chat session to be fully established...");
                vTaskDelay(pdMS_TO_TICKS(3000)); // 延迟3秒
                
                // 再次检查chat状态，确保连接已建立
                int retry_count = 0;
                const int max_retries = 5;
                while (retry_count < max_retries) {
                    esp_coze_chat_handle_t test_handle = coze_chat_get_handle();
                    if (test_handle) {
                        ESP_UTILS_LOGI("✅ Chat session is ready, proceeding to send message");
                        break;
                    } else {
                        retry_count++;
                        ESP_UTILS_LOGW("⚠️  Chat session not ready, retrying... (%d/%d)", retry_count, max_retries);
                        vTaskDelay(pdMS_TO_TICKS(1000)); // 每次重试等待1秒
                    }
                }
                
                // 只有在会话建立成功时才发送消息
                if (retry_count < max_retries) {
                    // 创建conversation.message.create事件
                    cJSON *event_json = cJSON_CreateObject();
                if (event_json) {
                    // 生成唯一的事件ID
                    static int event_counter = 0;
                    char event_id[32];
                    snprintf(event_id, sizeof(event_id), "%d%d%d", (int)(esp_timer_get_time() / 1000), (int)(esp_random() % 10000), ++event_counter);
                    
                    cJSON_AddStringToObject(event_json, "id", event_id);
                    cJSON_AddStringToObject(event_json, "event_type", "conversation.message.create");
                    
                    // 创建data对象
                    cJSON *data_obj = cJSON_CreateObject();
                    if (data_obj) {
                        cJSON_AddStringToObject(data_obj, "role", "user");
                        cJSON_AddStringToObject(data_obj, "content_type", "text");
                        cJSON_AddStringToObject(data_obj, "content", text_content);
                        cJSON_AddItemToObject(event_json, "data", data_obj);
                        
                        // 转换为JSON字符串
                        char *json_str = cJSON_PrintUnformatted(event_json);
                        if (json_str) {
                            ESP_UTILS_LOGI("📤 Sending conversation.message.create event: %s", json_str);
                            
                            // 获取chat句柄并发送自定义数据到AI平台
                            esp_coze_chat_handle_t chat_handle = coze_chat_get_handle();
                            esp_err_t send_result = ESP_FAIL;
                            
                            if (chat_handle) {
                                send_result = esp_coze_chat_send_customer_data(chat_handle, json_str);
                            } else {
                                ESP_UTILS_LOGE("❌ Chat handle not available - chat may not be started or connected");
                            }
                            if (send_result == ESP_OK) {
                                ESP_UTILS_LOGI("✅ Text content sent to AI successfully");
                                snprintf(success_msg, sizeof(success_msg), "AI chat started and text sent successfully");
                            } else {
                                ESP_UTILS_LOGE("❌ Failed to send text content to AI: %s", esp_err_to_name(send_result));
                                snprintf(success_msg, sizeof(success_msg), "AI chat started but failed to send text");
                            }
                            
                            free(json_str);
                        } else {
                            ESP_UTILS_LOGE("❌ Failed to create JSON string for text content");
                        }
                    } else {
                        ESP_UTILS_LOGE("❌ Failed to create data object for text content");
                    }
                    
                    cJSON_Delete(event_json);
                } else {
                    ESP_UTILS_LOGE("❌ Failed to create event JSON for text content");
                }
                } else {
                    ESP_UTILS_LOGE("❌ Chat session failed to establish after %d retries", max_retries);
                    snprintf(success_msg, sizeof(success_msg), "AI chat started but session not established for text sending");
                }
            }
            
            if (success) {
                send_command_response(command, true, success_msg);
            } else {
                send_command_response(command, false, "Failed to start AI chat");
                ESP_UTILS_LOGE("❌ Command failed: start_chat - unknown error");
            }
        }
        
    } else {
        // 未知命令
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "Unknown command: %s", command);
        send_command_response(command, false, error_msg);
        ESP_UTILS_LOGW("❓ Unknown command received: %s", command);
    }
    
    cJSON_Delete(json);
}

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
        // 处理接收到的命令
        handle_websocket_command((char*)data->data_ptr, data->data_len);
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
