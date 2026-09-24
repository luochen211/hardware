# 实验八A：综合项目 — 智能农业监控（MQTT）

## 功能

1. SmartConfig配网（首次用ESP-TOUCH APP，之后NVS自动记忆）
2. MQTT连接公共Broker（broker.emqx.io:1883）
3. 每5秒发布模拟温湿度 JSON 到 `/school/学号/sensor`
4. 订阅 `/school/学号/cmd` 接收控制命令
5. OLED显示模拟温湿度和连接状态
6. WS2812 LED + 继电器风扇
7. 长按K1_C(GPIO22) 5秒清除配网

## 手机APP测试

手机或PC安装 **MQTTX** / **MQTT Dashboard**：
- 连接 broker.emqx.io:1883
- 订阅 `/school/2024001/sensor` → 看温湿度数据
- 发布命令到 `/school/2024001/cmd`：

| 命令 | 效果 |
|------|------|
| `led_on` | LED亮红灯 |
| `led_off` | LED灭 |
| `fan_on` | 风扇/继电器开 |
| `fan_off` | 风扇/继电器关 |

## 编译运行

```bash
cd exp8a_mqtt
idf.py set-target esp32
idf.py build flash monitor
```

## OLED显示

| 行 | 内容 |
|---|------|
| 0 | MQTT Connected |
| 1 | T:25.3C（模拟温度）|
| 2 | H:60.5%（模拟湿度）|
| 3 | Connecting MQTT |

## 引脚

| 外设 | 引脚 | 说明 |
|------|------|------|
| WS2812 | GPIO14 | SPI3驱动 |
| OLED | SCL=GPIO19, SDA=GPIO18 | I2C 0x3C |
| 风扇/继电器 | GPIO17 | GPIO开关控制 |
| 按键K1_C | GPIO22 | 长按5秒清除配网 |
