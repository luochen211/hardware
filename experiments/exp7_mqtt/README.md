# 实验七：MQTT 客户端 + SmartConfig配网

## 功能

1. SmartConfig配网（首次用ESP-TOUCH APP，之后NVS自动记忆）
2. 连接公共MQTT Broker（broker.emqx.io:1883）
3. 每5秒发布模拟温湿度 JSON 到 `/school/学号/sensor`
4. 订阅 `/school/学号/cmd`，接收 `led_on`/`led_off` 控制WS2812
5. OLED显示WiFi和MQTT连接状态
6. 长按K1_C(GPIO22) 5秒清除配网

## 配网

首次：手机安装 **ESP-TOUCH** APP → 输入WiFi密码 → 配网
之后：NVS自动记忆，无需再配网

## 测试

手机或PC安装 **MQTTX** / **MQTT Dashboard**：
- 连接 broker.emqx.io:1883
- 订阅 `/school/2024001/sensor` → 看传感器数据
- 发布 `led_on` 到 `/school/2024001/cmd` → LED亮红灯
- 发布 `led_off` 到 `/school/2024001/cmd` → LED灭

## 编译运行

```bash
cd exp7_mqtt
idf.py set-target esp32
idf.py build flash monitor
```

## OLED显示

| 行 | 内容 |
|---|------|
| 1 | WiFi OK! |
| 2 | SSID名 |
| 3 | Connecting MQTT → MQTT Connected |
