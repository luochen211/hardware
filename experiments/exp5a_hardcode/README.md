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
#define TCP_SERVER_IP     "192.168.1.100"   // ← 改成 PC 的 IP
#define TCP_SERVER_PORT   8080               // ← 改成服务器端口
```

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
```

## PC 端 TCP 测试服务器

### Python 服务器示例

```python
#!/usr/bin/env python3
import socket

HOST = '0.0.0.0'   # 监听所有网卡
PORT = 8080

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(5)

print(f"TCP 服务器启动，监听 {HOST}:{PORT} ...")

while True:
    conn, addr = s.accept()
    print(f"客户端连接: {addr}")
    while True:
        data = conn.recv(1024)
        if not data:
            break
        print(f"收到: {data.decode()}")
        conn.sendall(data)   # 回显
    print("客户端断开")
    conn.close()
```

运行：`python tcp_server.py`

## 编译与烧录

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## 预期输出

```
I (xxx) wifi: WiFi STA 初始化完成，正在连接 your_ssid ...
I (xxx) wifi: 获取到 IP 地址: 192.168.1.xxx
I (xxx) wifi: ✅ WiFi 连接成功
I (xxx) exp5: Socket 创建成功，正在连接 192.168.1.100:8080 ...
I (xxx) exp5: ✅ 已连接 TCP 服务器
I (xxx) exp5: 发送 [24 bytes]: {"temp":25,"humi":60}
I (xxx) exp5: 收到回显 [24 bytes]: {"temp":25,"humi":60}
```

## 断线重连机制

- 发送失败或服务器关闭连接后，自动关闭 socket
- 等待 5 秒后重新连接
- WiFi 断开时自动重试 5 次
