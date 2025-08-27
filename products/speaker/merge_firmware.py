#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 固件合并工具
将分散的固件文件合并为一个完整的固件文件，用于其他工具烧录
"""

import os
import sys
import argparse
from pathlib import Path

def merge_firmware(build_dir="build", output_file="merged_firmware.bin", flash_size=16*1024*1024):
    """
    合并固件文件
    
    Args:
        build_dir: build目录路径
        output_file: 输出文件名
        flash_size: 闪存大小（字节）
    """
    
    # 固件文件映射表（地址 -> 文件路径）
    firmware_parts = {
        0x0: "bootloader/bootloader.bin",
        0xb0000: "brookesia_speaker.bin", 
        0x8000: "partition_table/partition-table.bin",
        0xd000: "ota_data_initial.bin",
        0x10000: "srmodels/srmodels.bin",
        0xc4e000: "mmap_build/boot/anim_boot.bin",
        0x92f000: "mmap_build/emotion/anim_emotion.bin", 
        0xb0a000: "mmap_build/icon/anim_icon.bin",
        0x880000: "spiffs_data.bin"
    }
    
    # 检查build目录是否存在
    if not os.path.exists(build_dir):
        print(f"错误: build目录 '{build_dir}' 不存在")
        return False
    
    # 创建输出文件
    print(f"正在创建合并固件: {output_file}")
    print(f"闪存大小: {flash_size // (1024*1024)}MB")
    
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
