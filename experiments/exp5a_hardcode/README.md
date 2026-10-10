# 实验五：WiFi STA 连接 + TCP 客户端通信

## 功能概述

| 模块 | 说明 |
|------|------|
| WiFi STA | 连接路由器，获取 IP 地址 |
| TCP 客户端 | 连接 PC 服务器，循环发送 JSON 数据 |
| JSON 上报 | `{"temp":25,"humi":60}` 格式模拟温湿度数据 |
| 断线重连 | 连接断开后自动重连 |

## 配置

### 1. 修改 WiFi 名称和密码

编辑 `main/wifi_connect.c`：

```c
#define WIFI_SSID       "YOUR_WIFI_SSID"      // 改成你的 Wi-Fi 名称
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"  // 改成你的 Wi-Fi 密码
```

### 2. 修改 TCP 服务器地址

编辑 `main/main.c`：

```c
#define TCP_SERVER_IP     "192.168.110.222"   // ← 改成 PC 的 IP（与 ESP32 在同一个局域网）
#define TCP_SERVER_PORT   8080               // ← 改成服务器端口
```

PC 的 IP 可以在系统网络设置中查看（macOS：`ipconfig getifaddr en0`；Windows：`ipconfig`）。ESP32 与 PC 必须连接同一个路由器。

## 文件结构

```
exp5a_hardcode/
├── CMakeLists.txt          # 顶层 CMake
├── README.md
└── main/
    ├── CMakeLists.txt      # 组件 CMake
    ├── main.c              # 主程序（TCP 客户端）
    ├── wifi_connect.h      # WiFi 连接接口
    └── wifi_connect.c      # WiFi STA 实现
server.py                   # PC 端 TCP 测试服务器（与工程同目录）
```

## PC 端 TCP 测试服务器

工程目录下的 `server.py` 会监听 `0.0.0.0:8080`，收到 JSON 后原样回显。

运行：`python3 server.py`（先启动服务器，再给 ESP32 上电）

核心逻辑：

```python
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 8080))
s.listen(5)
while True:
    conn, addr = s.accept()          # 等待 ESP32 连接
    while data := conn.recv(1024):   # 循环收数据
        conn.sendall(data)           # 回显
    conn.close()
```

PC 防火墙若弹出提示，需允许 Python 接收入站连接，否则 ESP32 无法连接。

## 编译与烧录

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## 预期输出

```
I (xxx) wifi: WiFi STA 初始化完成，正在连接 your_ssid ...
I (xxx) wifi: 获取到 IP 地址: 192.168.110.xxx
I (xxx) wifi: ✅ WiFi 连接成功: SSID=your_ssid
I (xxx) exp5: Socket 创建成功，正在连接 192.168.110.222:8080 ...
I (xxx) exp5: ✅ 已连接 TCP 服务器 192.168.110.222:8080
I (xxx) exp5: 发送 [24 bytes]: {"temp":25,"humi":60}
I (xxx) exp5: 收到回显 [24 bytes]: {"temp":25,"humi":60}
```

PC 端 `server.py` 应同时打印 `客户端连接: ...` 和 `收到: {"temp":25,"humi":60}`，每 3 秒一行。

## 断线重连机制

- 发送失败或服务器关闭连接后，自动关闭 socket
- 等待 5 秒后重新连接
- WiFi 断开时自动重试 5 次

## 验收清单

- [ ] `idf.py build` 通过
- [ ] 串口出现 `WiFi 连接成功` 和 `获取到 IP 地址`，OLED 第 2 行显示 `IP:`
- [ ] `server.py` 打印客户端连接信息，并持续收到 JSON
- [ ] ESP32 串口出现 `收到回显`，OLED 第 3 行显示 `T:xxC H:xx%`，第 4 行显示 `TCP Connected!`
- [ ] 关闭 `server.py` 后，ESP32 打印 `连接断开` 并每 5 秒重试；重启服务器后自动恢复

> 温湿度为模拟数据（见 `main.c` 的 `read_sensor_data`），不是真实传感器读数。
