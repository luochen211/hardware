# 实验五B：SmartConfig配网 + TCP客户端

## 功能

1. **SmartConfig配网**（手机APP一键配网）
2. TCP客户端连接服务器
3. JSON数据上报
4. 断线自动重连

## 配网流程

### 首次使用
1. 手机安装 **ESP-TOUCH** APP
   - 安卓/iOS搜索"ESP-TOUCH"（Espressif官方）
   - GitHub: https://github.com/EspressifApp/esp-touch
2. 手机连接WiFi（**必须是2.4GHz**）
3. 打开ESP-TOUCH APP
4. 输入WiFi密码 → 点击"开始配网"
5. 等待5-10秒，ESP32自动连接并打印IP地址

### 之后使用
- **无需再配网**，WiFi凭据已保存在NVS中
- 上电自动连接

### 重新配网
凭据保存在 NVS 中，要重新走一遍 SmartConfig，需要清空 NVS 后重新烧录：

```bash
idf.py erase-flash
idf.py flash monitor
```

## TCP 服务器配置

配网只解决 Wi-Fi，ESP32 还需要知道 PC 的地址。编辑 `main/main.c`：

```c
#define TCP_SERVER_IP     "192.168.110.222"   // ← 改成 PC 的 IP（与 ESP32 在同一个局域网）
#define TCP_SERVER_PORT   8080                // ← 改成服务器端口
```

PC 的 IP 可用 `ipconfig getifaddr en0`（macOS）或 `ipconfig`（Windows）查看。注意：手机配网用的 Wi-Fi 必须与 PC 在同一个路由器下，否则 ESP32 连不上 PC。

### PC 端服务器

工程目录下的 `server.py` 监听 `0.0.0.0:8080`，收到 JSON 后原样回显：

```bash
python3 server.py
```

先启动服务器，再给 ESP32 上电。PC 防火墙若弹出提示，需允许 Python 接收入站连接。

## 编译运行

```bash
cd exp5b_smartconfig
idf.py set-target esp32
idf.py build flash monitor
```

## 串口输出示例

首次上电（无NVS记录）：
```
I (xxx) wifi: 启动SmartConfig配网...
I (xxx) wifi:   请打开手机APP「ESP-TOUCH」进行配网
I (xxx) wifi: [SmartConfig] ✅ 收到WiFi信息!
I (xxx) wifi: [SmartConfig] SSID=MyWiFi
I (xxx) wifi: WiFi凭据已保存到NVS
I (xxx) wifi: 获取到IP: 192.168.110.xxx
I (xxx) wifi: ✅ WiFi连接成功（SmartConfig配网）
I (xxx) exp5: 已连接 TCP 服务器 192.168.110.222:8080
I (xxx) exp5: 发送 [24 bytes]: {"temp":25,"humi":60}
I (xxx) exp5: 收到回显 [24 bytes]: {"temp":25,"humi":60}
```

之后上电（有NVS记录）：
```
I (xxx) wifi: 从NVS读取到WiFi信息: SSID=MyWiFi
I (xxx) wifi: 使用已保存的WiFi连接: MyWiFi
I (xxx) wifi: ✅ WiFi连接成功（已保存信息）
```

## 验收清单

- [ ] `idf.py build` 通过
- [ ] 首次上电，ESP-TOUCH 配网后串口出现 `WiFi凭据已保存到NVS` 和 `获取到IP`
- [ ] 断电重启后，串口出现 `从NVS读取到WiFi信息`，无需再次配网
- [ ] `server.py` 已启动，ESP32 串口出现 `收到回显`，OLED 第 3 行显示 `T:xxC H:xx%`
- [ ] 关闭 `server.py` 后，ESP32 打印 `连接断开` 并每 5 秒重试；重启服务器后自动恢复

> 温湿度为模拟数据（见 `main.c` 的 `read_sensor_data`），不是真实传感器读数。

## 与实验5A对比

| | 5A（硬编码） | 5B（SmartConfig） |
|---|---|---|
| WiFi配置 | 改代码重新编译 | 手机APP配网，无需改代码 |
| 首次耗时 | 2分钟 | 5-10分钟 |
| 多人使用 | 每人改代码 | 一套代码通用 |
| 教学价值 | 基础WiFi | 真实IoT产品场景 |
