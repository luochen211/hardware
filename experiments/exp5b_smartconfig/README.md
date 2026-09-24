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

## 编译运行

```bash
cd exp5b_smartconfig
idf.py set-target esp32
idf.py build flash monitor
```

## 串口输出示例

首次上电（无NVS记录）：
```
[wifi] 启动SmartConfig配网...
[wifi] ========================================
[wifi]   请打开手机APP「ESP-TOUCH」进行配网
[wifi] ========================================
[wifi] [SmartConfig] ✅ 收到WiFi信息!
[wifi] [SmartConfig] SSID=MyWiFi
[wifi] WiFi凭据已保存到NVS
[wifi] 获取到IP: 192.168.1.100
[wifi] ✅ WiFi连接成功（SmartConfig配网）
```

之后上电（有NVS记录）：
```
[wifi] 从NVS读取到WiFi信息: SSID=MyWiFi
[wifi] 使用已保存的WiFi连接: MyWiFi
[wifi] ✅ WiFi连接成功（已保存信息）
```

## 与实验5A对比

| | 5A（硬编码） | 5B（SmartConfig） |
|---|---|---|
| WiFi配置 | 改代码重新编译 | 手机APP配网，无需改代码 |
| 首次耗时 | 2分钟 | 5-10分钟 |
| 多人使用 | 每人改代码 | 一套代码通用 |
| 教学价值 | 基础WiFi | 真实IoT产品场景 |
