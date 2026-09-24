# 实验3B：智慧农业板传感器

## 功能

| 传感器 | 引脚 | 说明 |
|--------|------|------|
| 土壤湿度 | GPIO34 (ADC1_CH6) | 模拟量 |
| 光照 | GPIO35 (ADC1_CH7) | 模拟量 |
| 雨滴 | GPIO33 (ADC1_CH5) | 模拟量 |
| OLED | SCL=GPIO19, SDA=GPIO18 | I2C, 地址0x3C |

> GPIO34/35是ESP32输入专用引脚，只能做ADC，无上拉/下拉。

## 编译运行

```bash
cd exp3b_farm
idf.py set-target esp32
idf.py build flash monitor
```

## 预期输出

```
土壤湿度: ADC=1500
光照: ADC=2000
雨滴: ADC=800
```

OLED显示4行：土壤、光照、雨滴ADC值、循环计数。
