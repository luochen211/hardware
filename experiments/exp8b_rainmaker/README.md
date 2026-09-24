# 实验八B：综合项目 — 智能家居（ESP RainMaker）

## 功能

1. BLE Provisioning配网（ESP RainMaker APP自带配网+认证）
2. RainMaker云平台连接（自动Assisted Claiming）
3. 每5秒尝试读取 DHT11 温湿度与烟雾 ADC，并上报云端；DHT11 读取失败时保留上次值（初值为 25°C、60%）
4. 手机 APP 控制 WS2812、继电器、蜂鸣器和步进电机
5. OLED 显示温湿度和连接状态

## 编译运行

本项目自带 components/ 目录（从 esp-rainmaker 仓库提取），可以独立编译：

```powershell
cd exp8b_rainmaker
idf.py set-target esp32
idf.py menuconfig
#   Serial flasher config → Flash size → 8 MB
#   Component config → Wi-Fi → WiFi IRAM speed optimization → 取消勾选
idf.py build flash monitor
```

## 手机APP

下载 **ESP RainMaker**（Espressif官方）：
- iOS: App Store 搜索 "ESP RainMaker"
- Android: Google Play

### 使用步骤

1. 注册/登录 Espressif 账号
2. 点 + Add Device
3. 复制串口打印的QR码URL到浏览器打开，扫描二维码
4. APP蓝牙发现设备 → 输入WiFi密码 → 自动Claim
5. 配网成功后控制界面：
   - LED：开关（控制WS2812红灯）
   - Relay：开关继电器
   - Temperature / Humidity：最近一次有效 DHT11 读数；首次读取失败时显示初值
   - Smoke：烟雾 ADC 读数
   - Buzzer / Motor：控制蜂鸣器和步进电机

## 注意事项

- 首次编译会从 Component Registry 下载 esp_rainmaker、qrcode、network_provisioning、esp_insights、button 等依赖
- 如果编译报 IRAM 溢出，在 menuconfig 关闭 WiFi IRAM speed optimization
- 如果运行报 `BTC_TASK stack overflow`，增大 BTC 任务栈：menuconfig → Component config → Bluetooth → Bluedroid Options → BT/BLE BTC Task Stack Size → 8192
- components/ 目录下的 3 个公共组件从 esp-rainmaker/examples/common/ 提取

## 引脚

| 外设 | 引脚 | 说明 |
|------|------|------|
| WS2812 | GPIO14 | SPI3驱动 |
| OLED | SCL=GPIO19, SDA=GPIO18 | I2C 0x3C |
| 继电器 | GPIO17 | GPIO 开关控制 |
| DHT11 | GPIO5 | 温湿度采集 |
| 烟雾传感器 | GPIO35 | ADC 采集 |
| 蜂鸣器 | GPIO13 | GPIO 控制 |
| 步进电机 | GPIO26/25/33/32 | 四相控制 |
