# 实验六B：BLE Provisioning 配网

## 功能

1. **首次**：手机APP「ESP BLE Prov」通过蓝牙配网连接WiFi
2. 配网成功后自动连接WiFi
3. WiFi凭据保存在NVS，下次上电自动连接
4. 长按K1_C(GPIO22) 5秒可清除配网信息

> **注意**：本实验只做配网，BLE GATT控制LED见实验6A。
> ESP32只有一套蓝牙控制器，NimBLE配网和Bluedroid GATT不能共存于同一固件。

## APP设置

1. 手机安装 **ESP BLE Prov**（Espressif官方）
2. APP设置里选 **Security Version 0**
3. 扫描找到 **PROV_IOT_Student**
4. 选择WiFi → 输入密码 → 配网

## 清除配网（换WiFi场景）

上电时按住 **K1_C** 5秒 → 擦除WiFi凭据 → 重启进入配网模式

## 编译运行

```bash
cd exp6b_ble_prov
idf.py set-target esp32
idf.py build flash monitor
```

## 串口输出示例

首次：
```
[配网] Provisioning启动
========================================
  BLE Provisioning 已启动
  1. 手机安装「ESP BLE Prov」APP
  2. APP设置里选 Security Version 0
  3. 扫描找到 "PROV_IOT_Student"
  4. 选择WiFi，输入密码
========================================
[配网] 收到WiFi信息: SSID=MyWiFi
[配网] WiFi凭据验证成功!
WiFi已连接! IP: 192.168.1.100
============================
WiFi已连接，配网完成！
```

之后上电：
```
已配网: SSID=MyWiFi，直接连接WiFi
WiFi已连接! IP: 192.168.1.100
```
