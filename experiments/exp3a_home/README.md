# 实验3A：智能家居板传感器

## 功能

| 传感器 | 引脚 | 说明 |
|--------|------|------|
| DHT11温湿度 | GPIO5 | 单总线协议 |
| 烟雾MQ-2 | GPIO35 (ADC1_CH7) | 模拟量，12位ADC |
| OLED | SCL=GPIO19, SDA=GPIO18 | I2C, 地址0x3C |

## 编译运行

```bash
cd exp3a_home
idf.py set-target esp32
idf.py build flash monitor
```

## 预期输出

```
DHT11: T=25C H=60%
烟雾: ADC=1234
```

OLED显示3行：温湿度、烟雾ADC值、循环计数。
