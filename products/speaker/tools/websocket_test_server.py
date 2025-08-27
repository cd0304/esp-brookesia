#!/usr/bin/env python3
"""
WebSocket测试服务器
用于测试ESP32S3电子宠物的状态上报功能
"""

import asyncio
import websockets
import json
import logging
from datetime import datetime

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 存储连接的客户端
connected_clients = set()

async def handle_client(websocket, path):
    """处理WebSocket客户端连接"""
    client_id = id(websocket)
    client_addr = websocket.remote_address
    
    logger.info(f"客户端 {client_id} 连接: {client_addr}")
    connected_clients.add(websocket)
    
    try:
        async for message in websocket:
            try:
                # 解析JSON消息
                data = json.loads(message)
                logger.info(f"收到来自客户端 {client_id} 的消息:")
                logger.info(f"消息类型: {data.get('type', 'unknown')}")
                logger.info(f"设备ID: {data.get('device_id', 'unknown')}")
                
                if data.get('type') == 'device_status':
                    device_data = data.get('data', {})
                    logger.info("设备状态数据:")
                    logger.info(f"  运行时间: {device_data.get('continue_time', 0)} 秒")
                    logger.info(f"  触摸次数: {device_data.get('touch_num', 0)}")
                    logger.info(f"  晕倒次数: {device_data.get('faint_num', 0)}")
                    logger.info(f"  是否有粪便: {device_data.get('is_have_feces', False)}")
                    logger.info(f"  清理粪便次数: {device_data.get('cleanup_feces_num', 0)}")
                    logger.info(f"  遛狗次数: {device_data.get('walking_num', 0)}")
                    logger.info(f"  喂食次数: {device_data.get('feeding_num', 0)}")
                    logger.info(f"  饥饿程度: {device_data.get('hunger_level', 0)}")
                    logger.info(f"  健身卡路里: {device_data.get('fitness_calories', 0)} kcal")
                    
                    # 发送确认消息
                    response = {
                        "type": "status_ack",
                        "timestamp": datetime.now().isoformat(),
                        "device_id": data.get('device_id'),
                        "status": "received"
                    }
                    await websocket.send(json.dumps(response))
                    logger.info(f"已发送确认消息给客户端 {client_id}")
                
            except json.JSONDecodeError as e:
                logger.error(f"JSON解析错误: {e}")
                logger.error(f"原始消息: {message}")
                
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"客户端 {client_id} 连接关闭")
    except Exception as e:
        logger.error(f"处理客户端 {client_id} 时发生错误: {e}")
    finally:
        connected_clients.discard(websocket)
        logger.info(f"客户端 {client_id} 断开连接")

async def broadcast_message(message):
    """向所有连接的客户端广播消息"""
    if connected_clients:
        await asyncio.wait([
            client.send(message) for client in connected_clients
        ])

async def main():
    """主函数"""
    host = "0.0.0.0"
    port = 8080
    
    logger.info(f"启动WebSocket服务器: ws://{host}:{port}")
    logger.info("等待ESP32S3设备连接...")
    
    # 启动WebSocket服务器
    async with websockets.serve(handle_client, host, port):
        logger.info(f"WebSocket服务器已启动，监听端口 {port}")
        
        # 保持服务器运行
        await asyncio.Future()  # 无限等待

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("服务器被用户中断")
    except Exception as e:
        logger.error(f"服务器运行错误: {e}")
