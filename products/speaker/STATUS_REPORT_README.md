# 电子宠物状态上报系统

## 概述

这个系统为ESP32S3电子宠物设备提供了完整的状态管理和WebSocket上报功能。系统会自动收集设备的各种状态数据，并通过WebSocket连接定期上报到服务器。

## 功能特性

### 设备状态数据

系统会收集以下电子宠物状态数据：

- **continue_time**: 设备开机累计运行时长（秒）
- **touch_num**: 设备被抚摸的累计次数
- **faint_num**: 设备累计晕倒次数（基于IMU晃动检测）
- **is_have_feces**: 设备当前是否有排泄物（布尔值）
- **cleanup_feces_num**: 清理粪便的次数
- **walking_num**: 遛宠物的次数
- **feeding_num**: 喂食次数
- **hunger_level**: 饥饿程度（0-3，0=很饱，1=刚好，2=有点饿，3=快饿死）
- **fitness_calories**: 健身卡路里数（千卡）
- **device_id**: 设备唯一标识符

### 自动数据收集

系统会自动收集以下数据：

1. **触摸检测**: 当用户触摸设备时，自动增加触摸计数
2. **晃动检测**: 当IMU检测到晃动时，自动增加晕倒计数
3. **运行时间**: 自动累计设备运行时间
4. **数据持久化**: 所有数据自动保存到NVS存储中

### WebSocket上报

- 支持WebSocket连接自动重连
- 可配置上报间隔时间
- 支持立即上报功能
- 自动JSON格式数据封装

## 配置说明

### 修改WebSocket服务器地址

编辑 `products/speaker/main/modules/status_config.h` 文件：

```c
// WebSocket服务器配置
#define STATUS_REPORT_SERVER_URL "ws://your-server.com:8080/ws"
#define STATUS_REPORT_INTERVAL_SECONDS 30
```

### 服务器地址格式

- WebSocket: `ws://host:port/path`
- WebSocket Secure: `wss://host:port/path`

## 数据格式

### 上报的JSON数据格式

```json
{
  "type": "device_status",
  "device_id": "esp32s3_pet_12345678",
  "data": {
    "device_id": "esp32s3_pet_12345678",
    "continue_time": 3600,
    "touch_num": 15,
    "faint_num": 3,
    "is_have_feces": false,
    "cleanup_feces_num": 2,
    "walking_num": 5,
    "feeding_num": 8,
    "hunger_level": 1,
    "fitness_calories": 150
  }
}
```

## API接口

### 设备信息管理

```c
// 初始化设备信息模块
bool device_info_init();

// 获取设备状态JSON
char* get_device_info_json();

// 获取设备ID
const char* get_device_id();

// 增加抚摸次数
void increment_touch_count();

// 增加晕倒次数
void increment_faint_count();

// 增加清理粪便次数
void increment_cleanup_feces_count();

// 增加遛狗次数
void increment_walking_count();

// 增加喂食次数
void increment_feeding_count();

// 设置饥饿程度
void set_hunger_level(int level);

// 增加健身卡路里
void add_fitness_calories(int calories);

// 设置是否有粪便
void set_have_feces(bool have_feces);
```

### 状态上报管理

```c
// 初始化状态上报模块
bool status_report_init();

// 启动状态上报
bool status_report_start(const char* server_url);

// 停止状态上报
void status_report_stop();

// 立即发送设备状态
bool status_report_send_now();

// 设置上报间隔
void status_report_set_interval(int interval_seconds);

// 获取连接状态
bool status_report_is_connected();
```

## 使用示例

### 基本使用

```c
#include "modules/device_info.h"
#include "modules/status_report.h"

// 初始化
device_info_init();
status_report_init();

// 启动上报
status_report_start("ws://192.168.1.100:8080/ws");
status_report_set_interval(30);

// 手动更新状态
increment_touch_count();
increment_feeding_count();
set_hunger_level(1);

// 立即上报
status_report_send_now();
```

### 集成到现有功能

系统已经自动集成了以下功能：

1. **触摸检测**: 在 `system.cpp` 的触摸事件回调中自动增加触摸计数
2. **晃动检测**: 在 `imu_gesture.cpp` 的晃动检测中自动增加晕倒计数
3. **自动上报**: 在 `main.cpp` 中自动初始化和启动上报功能

## 测试功能

系统提供了测试函数：

```c
// 测试所有设备信息功能
test_device_info_functions();

// 测试获取设备信息
test_get_device_info_result();
```

## 注意事项

1. **网络连接**: 确保设备已连接到WiFi网络
2. **服务器地址**: 确保WebSocket服务器地址正确且可访问
3. **内存管理**: 调用 `get_device_info_json()` 后需要手动释放返回的字符串
4. **数据持久化**: 所有数据会自动保存到NVS存储中，重启后数据不会丢失

## 故障排除

### 常见问题

1. **WebSocket连接失败**
   - 检查网络连接
   - 验证服务器地址格式
   - 确认服务器是否运行

2. **数据上报失败**
   - 检查WebSocket连接状态
   - 查看日志输出
   - 验证JSON数据格式

3. **触摸/晃动检测不工作**
   - 检查硬件连接
   - 验证传感器初始化
   - 查看相关日志

### 调试日志

系统会输出详细的调试日志，可以通过日志查看：

- 设备状态变化
- WebSocket连接状态
- 数据上报情况
- 错误信息

## 扩展功能

### 添加新的状态数据

1. 在 `device_status_t` 结构体中添加新字段
2. 在 `get_device_info_json()` 中添加JSON字段
3. 添加相应的更新函数
4. 在适当的地方调用更新函数

### 自定义上报逻辑

1. 修改 `status_report_send_now()` 函数
2. 自定义JSON数据格式
3. 添加数据验证逻辑
4. 实现自定义上报策略
