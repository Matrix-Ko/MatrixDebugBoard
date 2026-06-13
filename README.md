# MatrixDebugBoard

<<<<<<< HEAD
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://github.com/espressif/esp-idf)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange)](https://www.espressif.com/)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Language](https://img.shields.io/badge/C-00599C?logo=c&logoColor=white)]()
[![Language](https://img.shields.io/badge/JavaScript-F7DF1E?logo=javascript&logoColor=black)]()
[![Python](https://img.shields.io/badge/Python-3.9+-3776AB?logo=python&logoColor=white)]()

An open-source, multi-protocol industrial debugging platform based on the **ESP32-S3** microcontroller. Integrates RS485, CAN (TWAI), dual UART channels, external SPI Flash file system, and provides a full-featured dark-theme Web UI accessible from any browser. Supports OTA remote firmware updates and AI-powered protocol analysis.

---

## Features

| Feature | Description |
|---------|-------------|
| **RS485** | Full-duplex UART2 with DE/RE auto-switching, Modbus frame format, CRC16 calculation |
| **CAN (TWAI)** | 500 kbps / 1 Mbps multi-rate, Normal / Listen-only / Self-test three modes |
| **Dual UART** | UART0 (IO43/44) + UART1 (IO17/18), independent baud rate, format, and logging |
| **External SPI Flash** | W25Q128 16 MB, custom sequential file system, log save/download/delete, format storage |
| **Web Console** | Pure JavaScript SPA, dark theme, Chinese/English bilingual, WebSocket real-time push |
| **WiFi AP+STA** | Fixed hotspot `192.168.4.1`, customizable SSID/password, STA credentials persisted in NVS |
| **OTA Update** | HTTP firmware pull, version comparison, A/B dual partition, crash auto-rollback |
| **AI Protocol Analysis** | OpenAI/Anthropic API integration for automatic bus data analysis |
| **Relay Control** | GPIO42, state persisted in NVS across power cycles |
| **Heartbeat LED** | GPIO2, 900ms off / 100ms on, indicates system running status |
| **Send History** | Last 20 records per channel, supports resend, edit, and single delete |

---

## Hardware Platform

| Item | Specification |
|------|---------------|
| **MCU** | ESP32-S3, Xtensa LX7 dual-core, up to 240 MHz |
| **Flash** | External W25Q128, 16 MB, SPI3 @ 40 MHz |
| **WiFi** | 802.11 b/g/n, AP+STA simultaneous operation |
| **CAN** | On-chip TWAI controller, GPIO6/7 |
| **RS485** | UART2 + DE/RE, GPIO8/9/46/16 |
| **Debug UART** | USB Serial/JTAG (IO19/IO20), frees UART0 for user use |
| **Partition** | Custom OTA dual partition, 3 MB each |

---

## Directory Structure

```
matrixdebugboard/
├── main/                           # Application entry point
│   ├── CMakeLists.txt
│   └── main.c                      # app_main: init all components, LED, relay
│
├── components/
│   ├── dashboard/                  # Web service core
│   │   ├── include/                #   dashboard.h, http_server.h, ws_server.h
│   │   ├── src/                    #   HTTP server, WebSocket, web_assets
│   │   └── web/                    #   SPA frontend
│   │       ├── index.html          #     Main frame
│   │       ├── app.js              #     Single-page app (2500+ lines)
│   │       ├── style.css           #     Dark theme styles
│   │       └── modules/            #     Feature module HTML files
│   │           ├── rs485.html      #       RS485 debugging
│   │           ├── can.html        #       CAN bus debugging
│   │           ├── uart.html       #       Dual UART debugging
│   │           ├── flash.html      #       File manager
│   │           ├── wifi.html       #       WiFi configuration
│   │           ├── system.html     #       System monitor
│   │           ├── ota.html        #       OTA firmware update
│   │           └── ai.html         #       AI protocol analysis
│   ├── can_twai/                   # CAN bus TWAI driver
│   ├── uart_485/                   # RS485 communication driver
│   ├── uart_debug/                 # Dual UART debug component
│   ├── spi_flash_ext/              # External SPI Flash file system
│   ├── wifi_module/                # WiFi AP+STA dual-mode manager
│   └── ota_manager/                # OTA firmware update manager
│
├── OTA_Server/                     # PC-side OTA firmware server (Flask)
│   ├── server.py
│   ├── config.json
│   ├── requirements.txt
│   └── README.md
│
├── hardware/                       # Hardware design files (Altium)
│   ├── PCB1.pcbdoc
│   └── Schematic1/
│       └── P1.schdoc
│
├── docs/                           # Documentation
│
├── tools/                          # Utility scripts
│
├── CMakeLists.txt                  # Project root CMake
├── partitions.csv                  # OTA partition table (16MB)
├── sdkconfig.defaults              # ESP-IDF default configuration
├── LICENSE                         # MIT License
└── .gitignore
=======
> 🚧 **正在整理中 / Organizing** — 资料整理完成后将上传代码和文档 / Code and documentation will be uploaded once organized.

---

## 📖 简介 / Introduction

**MatrixDebugBoard** 是一个基于 ESP32 的调试开发板项目，集成了多种硬件接口和调试功能，旨在为嵌入式开发提供一个高效、便捷的硬件调试工具。

**MatrixDebugBoard** is an ESP32-based debug development board project that integrates multiple hardware interfaces and debugging features, designed to provide an efficient and convenient hardware debugging tool for embedded development.

---

## ✨ 主要功能 / Features

*待整理上传 / To be organized and uploaded*

- 基于 ESP32 主控芯片 / Based on ESP32 main controller
- 多功能调试接口 / Multi-functional debugging interfaces
- OTA 远程升级支持 / OTA remote firmware update support
- 多种通信协议兼容 / Compatible with multiple communication protocols

---

## 📂 项目结构 / Project Structure

*待整理上传 / To be organized and uploaded*

```
MatrixDebugBoard/
├── Firmware/          # 固件源代码 / Firmware source code
├── hardware/          # 硬件设计文件 / Hardware design files
├── OTA_Server/        # OTA 升级服务器 / OTA update server
├── docs/              # 文档资料 / Documentation
└── tools/             # 工具脚本 / Utility scripts
>>>>>>> bc514964b058eb445dabf33d31d0cf1a1c9b127d
```

---

<<<<<<< HEAD
## Components

| Component | Description |
|-----------|-------------|
| **dashboard** | Core web server based on `esp_http_server`. Handles HTTP REST API, WebSocket real-time communication, dynamic route registration, and embeds web assets into firmware binary. The frontend is a tab-based SPA for RS485, CAN, UART, Flash, WiFi, System, OTA, and AI modules. |
| **can_twai** | ESP-IDF TWAI (Two-Wire Automotive Interface) driver wrapper. Supports Normal, Listen-only, and Self-test modes at configurable bit rates. Provides frame transmission, reception with timestamping, and bus status reporting. |
| **uart_485** | RS485 half-duplex communication via UART2. Implements DE/RE pin toggling for direction control, configurable baud rate/data bits/parity/stop bits, CRC16 calculation, and Modbus frame formatting. |
| **uart_debug** | Dual UART debug channels (UART0 on IO43/44, UART1 on IO17/18). Independent configuration per port, hex/ASCII display mode, and persistent send history. |
| **spi_flash_ext** | External SPI Flash (W25Q128) driver with a custom sequential file system. Supports read/write/erase operations, file listing via JSON API. |
| **wifi_module** | Dual-mode WiFi manager supporting simultaneous AP and STA modes. AP configuration persistence, STA credential storage in NVS, WiFi network scanning, automatic reconnection with configurable retry. |
| **ota_manager** | OTA firmware update manager. Performs HTTP firmware download, version comparison, A/B partition switching, and automatic rollback on boot failure. |
| **OTA_Server** | Lightweight Flask-based firmware update server for hosting firmware binaries and serving update manifests. |

---

## GPIO Pin Definitions

| Net Label | GPIO | Function |
|-----------|------|----------|
| TWAI_TX | IO6 | CAN bus transmit |
| TWAI_RX | IO7 | CAN bus receive |
| UART2_TX | IO8 | RS485 transmit data |
| UART2_RX | IO9 | RS485 receive data |
| UART2RE | IO16 | RS485 receive enable (active low) |
| UART2DE | IO46 | RS485 transmit enable (active high) |
| SPI3_MISO | IO35 | External Flash MISO |
| SPI3_CLK | IO36 | External Flash clock |
| SPI3_MOSI | IO37 | External Flash MOSI |
| FLASH_CS | IO38 | External Flash chip select |
| LED | IO2 | Status indicator |
| RELAY | IO42 | Relay output |
| UART0 RX/TX | RXD0/TXD0 | Debug serial (USB-UART bridge) |
| D- / D+ | IO19/IO20 | USB native (CDC backup) |

---

## Quick Start

### Prerequisites

- ESP-IDF v5.x ([installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/))
- ESP32-S3 development board
- USB cable for programming

### Build and Flash

```bash
git clone https://github.com/Matrix-Ko/MatrixDebugBoard.git
cd MatrixDebugBoard
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your actual port (e.g., `COM3` on Windows, `/dev/ttyACM0` on Linux).

### Access Web Interface

1. Power on the device and connect to its WiFi AP (SSID: `MatrixDebug`, password: `12345678`)
2. Open browser to `http://192.168.4.1`
3. The dark-theme Web UI loads with all debugging tools

---

## OTA Firmware Update

The project includes a lightweight Flask-based OTA server for remote firmware updates.

```bash
cd OTA_Server
pip install -r requirements.txt
python server.py
```

Upload new firmware:

```bash
curl -X POST http://your-server:8080/upload \
     -F "firmware=@build/matrixdebugboard.bin" \
     -F "version=1.1.0" \
     -F "notes=Release notes"
```

On the device Web UI, navigate to the OTA tab, enter the manifest URL, and check for updates.

---

## Configuration

Key ESP-IDF settings configured in `sdkconfig.defaults`:

| Setting | Value | Description |
|---------|-------|-------------|
| `CONFIG_IDF_TARGET` | `esp32s3` | Target chip |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | `y` | Auto-rollback on boot failure |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `y` | Console via USB Serial/JTAG |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` | `y` | 16 MB flash |
| `CONFIG_COMPILER_OPTIMIZATION_NONE` | `y` | -O0 to avoid GCC ICE |
| `CONFIG_PARTITION_TABLE_CUSTOM` | `y` | Custom OTA partition table |
| `CONFIG_HTTPD_WS_SUPPORT` | `y` | WebSocket support |

---

## HTTP API Reference

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | System status (WiFi, IP, uptime, free heap, relay state) |
| GET | `/api/hist?key=rs485` | Get send history for channel |
| POST | `/api/hist?key=rs485` | Save send history for channel |
| WebSocket | `/ws` | Real-time bidirectional communication |

---

## WebSocket Protocol

All real-time communication uses JSON-formatted messages over WebSocket.

### Client → Device

```json
{"type":"rs485_send","data":"01030000000205C8","hex":true}
{"type":"rs485_cfg","baud":9600,"mode":"hex"}
{"type":"can_send","id":291,"ext":false,"data":"DEADBEEF01020304"}
{"type":"can_cfg","baud":500000,"mode":"normal"}
{"type":"flash_read","addr":0,"len":256}
{"type":"flash_write","addr":0,"data":"FF00AA55..."}
{"type":"flash_erase","addr":0,"size":4096}
{"type":"flash_info"}
{"type":"uart_send","port":0,"data":"Hello","hex":false}
{"type":"uart_cfg","port":0,"baud":115200}
```

### Device → Client

```json
{"type":"rs485_rx","ts":12345,"data":"01030200640195"}
{"type":"can_rx","ts":12345,"id":291,"ext":false,"dlc":8,"data":"DEADBEEF01020304"}
{"type":"can_status","state":"running","tx_err":0,"rx_err":0}
{"type":"flash_data","addr":0,"data":"FF00AA55..."}
{"type":"flash_info","total":16777216,"used":65536,"files":[{"name":"log.dat","size":65536}]}
{"type":"sys_info","ip":"192.168.4.2","uptime":3600,"heap":180000,"relay":1}
```

---

## Build from Source

```bash
git clone https://github.com/Matrix-Ko/MatrixDebugBoard.git
cd MatrixDebugBoard
idf.py set-target esp32s3
idf.py menuconfig   # optional
idf.py build
idf.py -p PORT flash
idf.py -p PORT monitor
```

---

## License

This work is licensed under a **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License**.

[![CC BY-NC-SA 4.0](https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png)](LICENSE)

- ✅ **Share** — copy and redistribute the material in any medium or format
- ✅ **Adapt** — remix, transform, and build upon the material
- ❌ **NonCommercial** — You may not use the material for commercial purposes
- 🔗 **ShareAlike** — If you remix, transform, or build upon the material, you must distribute your contributions under the same license

Full license text is available in the [LICENSE](./LICENSE) file.

---

## Contact

- **Author**: [Matrix-Ko](https://github.com/Matrix-Ko)
