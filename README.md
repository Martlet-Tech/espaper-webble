# 电子纸 greeting sign 项目

## 项目概述

这是一个基于 ESP32 的电子纸显示项目，支持通过蓝牙和WiFi两种方式更新显示内容，并实时查询传输进度。

### 支持的芯片
| 芯片型号 | 支持状态 |
|---------|---------|
| ESP32 | ✅ |
| ESP32-C2 | ✅ |
| ESP32-C3 | ✅ |
| ESP32-C6 | ✅ |
| ESP32-S3 | ✅ |

## 功能特性

### 1. 蓝牙BLE功能
- 设备名称：`YESEPD_GATTS_DEMO`
- 支持的命令：
  - `CMD_RESET_EPD (0x00)` - 重置电子纸
  - `CMD_SET_EPD_NAME (0x01)` - 设置电子纸名称
  - `CMD_REPORT_EPD_INFO (0x02)` - 报告电子纸信息
  - `CMD_START_WRITE_DATA (0x03)` - 开始写入数据
  - `CMD_CURRENT_PACKET_INDEX (0x04)` - 当前数据包索引和数据
  - `CMD_END_WRITE_DATA (0x05)` - 结束写入数据
  - `CMD_EPD_CLEAR (0x06)` - 清除电子纸屏幕
  - `CMD_BATTERY_LEVEL (0x07)` - 电池电量
  - `CMD_SET_WIFI (0x08)` - 设置WiFi SSID和密码
  - `CMD_SET_WORKING_MODE (0x09)` - 设置工作模式
  - `CMD_SET_CUSTOM_NAME (0x0A)` - 设置自定义名称
  - `CMD_QUERY_PROGRESS (0x0B)` - 查询传输进度

### 2. WiFi功能
- 支持通过蓝牙设置WiFi配置
- 自动重连已保存的WiFi网络
- 提供Web服务器，支持HTTP POST上传图片

### 3. Web服务器
- 接收图片数据（POST请求）
- 实时显示传输进度
- 支持跨域请求

### 4. 电子纸显示
- 支持用户上传图片显示
- 支持屏幕清除功能
- 支持不同工作模式

### 5. 进度查询
- 通过蓝牙查询WiFi传输进度
- 实时返回百分比进度

## 硬件要求

- ESP32/ESP32-C3/ESP32-S3/ESP32-C2 开发板
- 电子纸显示屏（支持多种型号）
- USB数据线（供电和编程）
- 电源供应（推荐5V 2A）

## 快速开始

### 1. 配置和构建

1. 设置芯片目标：
```bash
idf.py set-target <chip_name>
```

2. 构建项目：
```bash
idf.py build
```

3. 烧录到设备：
```bash
idf.py -p <PORT> flash monitor
```

### 2. 首次使用

1. **通过蓝牙配置WiFi**：
   - 使用支持BLE的应用连接到 `YESEPD_GATTS_DEMO`
   - 发送 `CMD_SET_WIFI (0x08)` 命令设置WiFi SSID和密码

2. **通过Web上传图片**：
   - 连接到配置的WiFi网络
   - 找到设备IP地址（查看串口输出）
   - 在浏览器中访问设备IP
   - 上传图片文件

3. **查询传输进度**：
   - 通过蓝牙发送 `CMD_QUERY_PROGRESS (0x0B)` 命令
   - 接收返回的进度数据（2字节，大端格式）

## API说明

### 蓝牙API

#### 命令格式
所有命令都通过BLE特征值写入，格式为：
```
[命令字节] + [参数数据]
```

#### 主要命令详解

1. **设置WiFi** (`0x08`)
   - 格式：`0x08 + [ssid长度] + [ssid] + [密码长度] + [密码]`
   - 示例：`0x08 0x04 0x54 0x45 0x53 0x54 0x04 0x31 0x32 0x33 0x34` (设置SSID为"TEST"，密码为"1234")

2. **查询进度** (`0x0B`)
   - 格式：`0x0B`
   - 返回：2字节进度值（大端格式），范围0-100

### Web API

#### POST /
- 描述：上传图片数据
- 内容类型：`application/octet-stream`
- 响应：`Data received successfully!`

#### GET /
- 描述：获取Web界面
- 响应：HTML上传页面

#### OPTIONS /
- 描述：处理跨域请求
- 响应：CORS头信息

## 工作流程

1. **设备启动**：
   - 初始化蓝牙和WiFi
   - 尝试自动重连已保存的WiFi

2. **WiFi配置**：
   - 通过蓝牙接收WiFi配置
   - 连接到指定WiFi网络
   - 启动Web服务器

3. **图片上传**：
   - 接收HTTP POST请求
   - 实时更新传输进度
   - 存储到PSRAM

4. **显示更新**：
   - 触发电子纸刷新
   - 显示上传的图片

5. **进度查询**：
   - 蓝牙接收查询命令
   - 返回当前传输进度

## 故障排除

### 常见问题

1. **无法连接蓝牙**
   - 检查设备是否在蓝牙范围内
   - 确保设备已启动并处于可发现状态

2. **WiFi连接失败**
   - 检查WiFi SSID和密码是否正确
   - 确保WiFi信号强度足够

3. **图片上传失败**
   - 检查图片大小是否超过设备内存
   - 确保网络连接稳定

4. **进度查询返回0%**
   - 确保WiFi传输正在进行中
   - 检查蓝牙连接是否正常

### 日志输出

设备启动后会在串口输出详细日志，包括：
- WiFi连接状态
- Web服务器启动信息
- 图片传输进度
- 蓝牙命令处理

## 技术规格

- **蓝牙**：BLE 4.2+
- **WiFi**：802.11 b/g/n
- **Web服务器**：ESP-IDF HTTP Server
- **内存**：使用PSRAM存储图片数据
- **传输速度**：取决于网络环境，典型值50-100KB/s

## 开发说明

### 代码结构

- `main/ble/` - 蓝牙GATT服务实现
- `main/wifi/` - WiFi和Web服务器实现
- `main/lcd/` - 电子纸驱动
- `main/` - 主应用逻辑

### 配置选项

- `MAX_IMAGE_SIZE` - 最大图片大小（默认800KB）
- `DATA_CHUNK_SIZE` - BLE数据传输块大小（默认490字节）

## 许可证

MIT License

## 联系方式

- 作者：zhaitao (zhaitao.as@outlook.com)
- 项目地址：https://github.com/yourusername/Greeting_Sign

---

**注意**：本项目仅供学习和研究使用，商业使用请联系作者。