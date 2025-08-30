#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 固件合并工具
将分散的固件文件合并为一个完整的固件文件，用于其他工具烧录
"""

import os
import sys
import argparse
import json
from pathlib import Path

def load_flash_args(build_dir):
    """
    从构建目录加载闪存参数
    
    Args:
        build_dir: build目录路径
        
    Returns:
        dict: 包含地址和文件路径映射的字典
    """
    flasher_args_path = os.path.join(build_dir, "flasher_args.json")
    
    if not os.path.exists(flasher_args_path):
        print(f"错误: 找不到 flasher_args.json 文件: {flasher_args_path}")
        print("请先构建项目 (idf.py build)")
        return None
    
    try:
        with open(flasher_args_path, 'r', encoding='utf-8') as f:
            flasher_args = json.load(f)
        
        flash_files = flasher_args.get("flash_files", {})
        if not flash_files:
            print("错误: flasher_args.json 中没有找到 flash_files")
            return None
            
        # 转换地址格式并创建映射
        firmware_parts = {}
        for addr_str, file_path in flash_files.items():
            addr = int(addr_str, 16)  # 将十六进制字符串转换为整数
            firmware_parts[addr] = file_path
            
        return firmware_parts
        
    except json.JSONDecodeError as e:
        print(f"错误: 解析 flasher_args.json 失败: {e}")
        return None
    except Exception as e:
        print(f"错误: 读取 flasher_args.json 失败: {e}")
        return None

def merge_firmware(build_dir="build", output_file="merged_firmware.bin", flash_size=16*1024*1024):
    """
    合并固件文件
    
    Args:
        build_dir: build目录路径
        output_file: 输出文件名
        flash_size: 闪存大小（字节）
    """
    
    # 从构建文件动态加载固件文件映射表
    firmware_parts = load_flash_args(build_dir)
    if firmware_parts is None:
        return False
    
    # 检查build目录是否存在
    if not os.path.exists(build_dir):
        print(f"错误: build目录 '{build_dir}' 不存在")
        return False
    
    # 创建输出文件
    print(f"正在创建合并固件: {output_file}")
    print(f"闪存大小: {flash_size // (1024*1024)}MB")
    print(f"发现 {len(firmware_parts)} 个固件分区:")
    
    # 显示所有分区信息
    for addr, file_path in sorted(firmware_parts.items()):
        print(f"  0x{addr:08x} -> {file_path}")
    print()
    
    # 初始化固件数据（用0xFF填充）
    firmware_data = bytearray([0xFF] * flash_size)
    
    # 按地址排序处理固件文件
    for addr, file_path in sorted(firmware_parts.items()):
        full_path = os.path.join(build_dir, file_path)
        
        if not os.path.exists(full_path):
            print(f"警告: 文件不存在 {full_path}")
            continue
            
        # 读取文件内容
        with open(full_path, 'rb') as f:
            data = f.read()
            
        print(f"合并: 0x{addr:08x} - {file_path} ({len(data)} 字节)")
        
        # 检查地址范围
        if addr + len(data) > flash_size:
            print(f"错误: 文件 {file_path} 超出闪存范围")
            return False
            
        # 写入数据
        firmware_data[addr:addr+len(data)] = data
    
    # 写入合并后的固件文件
    with open(output_file, 'wb') as f:
        f.write(firmware_data)
    
    print(f"\n✅ 固件合并完成!")
    print(f"输出文件: {output_file}")
    print(f"文件大小: {len(firmware_data) // (1024*1024)}MB")
    
    # 验证合并结果
    print("\n验证信息:")
    print(f"合并的分区数量: {len([addr for addr, _ in sorted(firmware_parts.items()) if os.path.exists(os.path.join(build_dir, firmware_parts[addr]))])}")
    print(f"跳过的分区数量: {len([addr for addr, _ in sorted(firmware_parts.items()) if not os.path.exists(os.path.join(build_dir, firmware_parts[addr]))])}")
    
    return True

def main():
    parser = argparse.ArgumentParser(description='ESP32-S3 固件合并工具')
    parser.add_argument('--build-dir', default='build', help='build目录路径 (默认: build)')
    parser.add_argument('--output', default='merged_firmware.bin', help='输出文件名 (默认: merged_firmware.bin)')
    parser.add_argument('--flash-size', type=int, default=16*1024*1024, help='闪存大小字节 (默认: 16MB)')
    
    args = parser.parse_args()
    
    success = merge_firmware(args.build_dir, args.output, args.flash_size)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
