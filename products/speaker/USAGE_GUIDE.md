# ESP32S3电子宠物状态上报系统使用指南

## 快速开始

### 1. 编译和烧录

1. 进入项目目录：
```bash
cd products/speaker
```

2. 配置项目：
```bash
idf.py menuconfig
```

3. 编译项目：
```bash
idf.py build
```

4. 烧录到设备：
```bash
idf.py flash monitor
```

### 2. 配置WebSocket服务器地址

编辑 `products/speaker/main/modules/status_config.h` 文件，修改服务器地址：

```c
#define STATUS_REPORT_SERVER_URL "ws://192.168.1.100:8080/ws"
```

### 3. 启动测试服务器

在电脑上运行WebSocket测试服务器：

```bash
cd products/speaker/tools
python3 websocket_test_server.py
```

服务器将在 `ws://0.0.0.0:8080` 上启动。

## 功能测试

### 自动功能

系统启动后会自动：

1. **初始化设备信息模块** - 加载历史数据
2. **启动状态上报** - 连接到WebSocket服务器
3. **开始数据收集** - 自动收集触摸、晃动等数据

### 手动测试

在设备串口控制台中，你可以使用以下命令进行测试：

```
# 运行一次完整的状态测试
status_test_run_once()

# 启动周期性测试（每10秒执行一次）
status_test_start()

# 停止周期性测试
status_test_stop()

# 测试设备信息功能
test_device_info_functions()

# 获取当前设备信息
test_get_device_info_result()
```

### 触摸测试

1. 触摸设备表面
2. 观察串口日志中的触摸计数增加
3. 检查WebSocket服务器是否收到状态更新

### 晃动测试

1. 晃动设备
2. 观察串口日志中的晕倒计数增加
3. 检查WebSocket服务器是否收到状态更新

## 数据格式说明

### 设备状态JSON格式

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

### 字段说明

| 字段名 | 类型 | 说明 | 范围 |
|--------|------|------|------|
| continue_time | Number | 设备开机累计运行时长（秒） | 0-∞ |
| touch_num | Number | 设备被抚摸的累计次数 | 0-∞ |
| faint_num | Number | 设备累计晕倒次数 | 0-∞ |
| is_have_feces | Boolean | 设备当前是否有排泄物 | true/false |
| cleanup_feces_num | Number | 清理粪便的次数 | 0-∞ |
| walking_num | Number | 遛宠物的次数 | 0-∞ |
| feeding_num | Number | 喂食次数 | 0-∞ |
| hunger_level | Number | 饥饿程度 | 0-3 |
| fitness_calories | Number | 健身卡路里数（千卡） | 0-∞ |

### 饥饿程度说明

- 0: 很饱
- 1: 刚好
- 2: 有点饿
- 3: 快饿死

## 配置选项

### 状态上报配置

在 `status_config.h` 中可以配置：

```c
// WebSocket服务器地址
#define STATUS_REPORT_SERVER_URL "ws://192.168.1.100:8080/ws"

// 上报间隔（秒）
#define STATUS_REPORT_INTERVAL_SECONDS 30

// 设备状态自动更新
#define DEVICE_STATUS_AUTO_UPDATE_ENABLED 1

// 数据保存间隔（秒）
#define DEVICE_STATUS_SAVE_INTERVAL_SECONDS 60

// 调试模式
#define STATUS_REPORT_DEBUG_ENABLED 1
```

### 运行时配置

可以通过API动态配置：

```c
// 设置上报间隔
status_report_set_interval(60); // 60秒上报一次

// 立即上报
status_report_send_now();

// 检查连接状态
if (status_report_is_connected()) {
    printf("WebSocket已连接\n");
}
```

## 故障排除

### 常见问题

1. **WebSocket连接失败**
   - 检查网络连接
   - 验证服务器地址
   - 确认服务器是否运行

2. **数据上报失败**
   - 检查WebSocket连接状态
   - 查看串口日志
   - 验证JSON数据格式

3. **触摸/晃动检测不工作**
   - 检查硬件连接
   - 验证传感器初始化
   - 查看相关日志

### 调试日志

系统会输出详细的调试日志，包括：

- 设备状态变化
- WebSocket连接状态
- 数据上报情况
- 错误信息

### 日志级别

可以通过 `menuconfig` 调整日志级别：

```
Component config → Log output → Default log verbosity
```

## 扩展开发

### 添加新的状态数据

1. 在 `device_info.c` 的 `device_status_t` 结构体中添加新字段
2. 在 `get_device_info_json()` 中添加JSON字段
3. 添加相应的更新函数
4. 在适当的地方调用更新函数

### 自定义上报逻辑

1. 修改 `status_report.c` 中的 `status_report_send_now()` 函数
2. 自定义JSON数据格式
3. 添加数据验证逻辑
4. 实现自定义上报策略

### 集成到其他功能

1. 在需要的地方调用状态更新函数
2. 监听相关事件并更新状态
3. 添加新的传感器数据收集

## 性能优化

### 内存使用

- 定期清理不需要的JSON字符串
- 使用静态缓冲区减少内存分配
- 优化数据结构大小

### 网络优化

- 调整上报间隔减少网络流量
- 实现数据压缩
- 添加重连机制

### 存储优化

- 定期清理过期数据
- 使用增量更新减少存储写入
- 实现数据压缩存储

## 安全考虑

### 网络安全

- 使用WSS（WebSocket Secure）连接
- 实现身份验证机制
- 添加数据加密

### 数据安全

- 验证接收到的数据
- 防止缓冲区溢出
- 实现数据完整性检查

## 维护和更新

### 定期维护

- 检查日志文件
- 监控系统性能
- 更新固件版本

### 数据备份

- 定期备份NVS数据
- 导出设备状态数据
- 实现数据恢复机制
