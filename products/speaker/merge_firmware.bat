@echo off
chcp 65001 >nul
echo ========================================
echo ESP32-S3 固件合并工具
echo ========================================
echo.

REM 检查Python是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未找到Python，请先安装Python
    pause
    exit /b 1
)

REM 检查build目录是否存在
if not exist "build" (
    echo 错误: build目录不存在，请先编译项目
    pause
    exit /b 1
)

echo 正在合并固件文件...
python merge_firmware.py

if errorlevel 1 (
    echo.
    echo ❌ 固件合并失败
    pause
    exit /b 1
) else (
    echo.
    echo ✅ 固件合并成功完成！
    echo 输出文件: merged_firmware.bin
    echo.
    echo 现在可以使用其他工具烧录 merged_firmware.bin 文件
)

pause
