# MatrixDebugBoard OTA Server

基于 Flask 的轻量化固件更新服务器，供 ESP32-S3 通过 HTTP 进行远程 OTA 升级。

---

## 目录结构

```
OTA_server/
├── server.py           # Flask 服务器主程序
├── config.json         # 当前发布版本配置
├── requirements.txt    # Python 依赖
├── firmware/           # 固件存放目录（手动创建或首次启动自动创建）
│   └── matrixdebugboard_v1.1.0.bin
└── README.md
```

---

## 环境要求

- Python 3.9+
- pip

---

## 安装

```powershell
cd OTA_server

# 建议使用虚拟环境
python -m venv .venv
.venv\Scripts\Activate.ps1

pip install -r requirements.txt
```

---

## 快速启动

```powershell
python server.py
```

默认监听 `0.0.0.0:8080`，所有网络接口均可访问。

自定义地址和端口：

```powershell
python server.py --host 0.0.0.0 --port 8080
```

启动后终端输出示例：

```
2026-05-18 21:00:00 [INFO] Config loaded — version=1.1.0  firmware=matrixdebugboard_v1.1.0.bin  file_exists=True
2026-05-18 21:00:00 [INFO] OTA Server starting on http://0.0.0.0:8080
2026-05-18 21:00:00 [INFO] Manifest : http://<your-ip>:8080/manifest.json
2026-05-18 21:00:00 [INFO] Status   : http://<your-ip>:8080/status
```

---

## 发布新版本固件

### 方式一：手动放置文件（推荐）

1. 将编译好的 `.bin` 文件放入 `firmware/` 目录：
   ```
   firmware/matrixdebugboard_v1.1.0.bin
   ```

2. 修改 `config.json`：
   ```json
   {
     "version":  "1.1.0",
     "filename": "matrixdebugboard_v1.1.0.bin",
     "notes":    "更新说明"
   }
   ```

3. 无需重启服务器，下次请求时自动生效。

---

### 方式二：通过 HTTP 上传（适合远程部署）

```powershell
curl -X POST http://192.168.1.10:8080/upload `
     -F "firmware=@build\matrixdebugboard.bin" `
     -F "version=1.1.0" `
     -F "notes=功能更新"
```

上传成功后 `config.json` 自动更新，文件写入 `firmware/` 目录。

---

## API 接口说明

### GET /manifest.json

ESP32 检测更新时调用。返回当前版本信息和固件下载地址。

**响应示例：**
```json
{
  "version": "1.1.0",
  "url":     "http://192.168.1.10:8080/firmware/matrixdebugboard_v1.1.0.bin",
  "notes":   "功能更新"
}
```

---

### GET /firmware/\<filename\>

下载固件二进制文件。响应包含 `Content-Length`，ESP32 据此显示下载进度。

---

### GET /status

查看服务器运行状态，用于连通性验证。

**响应示例：**
```json
{
  "server":        "MatrixDebugBoard OTA Server",
  "status":        "running",
  "version":       "1.1.0",
  "firmware":      "matrixdebugboard_v1.1.0.bin",
  "firmware_size": 1315792,
  "notes":         "功能更新",
  "manifest_url":  "http://192.168.1.10:8080/manifest.json",
  "firmware_url":  "http://192.168.1.10:8080/firmware/matrixdebugboard_v1.1.0.bin",
  "time":          "2026-05-18 21:05:00"
}
```

---

### POST /upload

上传新固件并自动更新配置。表单字段：

| 字段       | 类型   | 必填 | 说明             |
|-----------|--------|------|-----------------|
| `firmware` | file   | 是   | `.bin` 固件文件  |
| `version`  | string | 是   | 版本号，如 `1.1.0` |
| `notes`    | string | 否   | 更新说明         |

---

## 查询本机 IP

```powershell
ipconfig | findstr "IPv4"
```

- 连接路由器时（STA 模式）：使用局域网 IP，如 `192.168.1.10`
- 连接设备热点时（AP 模式）：PC 获得的 IP 通常为 `192.168.4.2`

---

## 在设备 Web UI 中配置 OTA

1. 打开 `http://192.168.4.1`（AP 模式）或设备 STA IP
2. 进入 **OTA升级** 标签页
3. 填写检测地址：
   ```
   http://<本机IP>:8080/manifest.json
   ```
4. 点击 **保存配置**
5. 点击 **检测更新** — 若远端版本高于当前版本，显示「有新版本」
6. 点击 **立即升级** — 进度条推进，约 15～30 秒后设备自动重启
7. 重启完成后版本号更新即为成功

---

## 构建新版本固件

```powershell
cd ..   # 项目根目录

# 修改 CMakeLists.txt 中的版本号
# set(PROJECT_VER "1.1.0")

idf.py build

# 将生成的 bin 复制到 OTA_server/firmware/
copy build\matrixdebugboard.bin OTA_server\firmware\matrixdebugboard_v1.1.0.bin
```

---

## 常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| 检测结果：「已是最新」 | `config.json` 版本号不大于设备当前版本 | 调高 version 字段 |
| 连接失败 | PC 防火墙拦截 8080 端口 | 见下方防火墙配置 |
| 下载中断 | 服务器意外停止或网络波动 | 保持终端开启；设备重新触发升级 |
| 固件校验失败 | `.bin` 文件不完整或来源错误 | 重新 `idf.py build` 生成 |

### 防火墙放行 8080 端口

```powershell
# 管理员 PowerShell
New-NetFirewallRule -DisplayName "MatrixDebugBoard OTA Server" `
    -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
```
