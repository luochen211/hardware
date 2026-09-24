# 实验4B：智慧农业板执行器控制

## 功能

| 执行器 | 引脚 | 驱动方式 |
|--------|------|---------|
| 风扇 | GPIO21 | GPIO 开关控制；非连续调速 |
| WS2812 RGB灯 | GPIO14 | SPI3驱动 |

## 实验内容

每按一次 K1_C（GPIO22）切换风扇状态：开启时 WS2812 显示绿色，关闭时熄灭。

## 编译运行

```bash
cd exp4b_farm
idf.py set-target esp32
idf.py build flash monitor
```

## 预期效果

- 风扇在关闭与全速之间切换，不支持 PWM 连续调速
- 风扇开启时彩灯为绿色，关闭时彩灯熄灭
