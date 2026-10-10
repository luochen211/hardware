# 实验2：FreeRTOS多任务 + GPIO + 按键检测

## 功能

| 任务 | 功能 |
|------|------|
| blink_task | WS2812 彩灯（GPIO14）：红色闪烁（1秒周期）或绿色呼吸灯 |
| button_task | K1_C(IO22)按键检测，消抖20ms，长按/短按判断 |
| print_task | 每5秒串口打印按键计数和LED模式 |

## 引脚

| 外设 | 引脚 | 说明 |
|------|------|------|
| WS2812 彩灯 | GPIO14 | SPI3 MOSI 驱动，智能家居板中间的 RGB 灯 |
| K1_C 拨动开关 | GPIO22 | 上拉输入，按下接地（实验主按键）|
| K1_D 拨动开关 | GPIO21 | 上拉输入，按下接地（备用，本实验未使用）|

> 引脚以 `main/main.c` 为准。README 早先写的"IO2 板载蓝灯"与代码不符，已更正。

## 编译运行

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## 操作说明

| 操作 | 效果 |
|------|------|
| 短按 K1_C | 切换回红色闪烁模式 |
| 长按 K1_C (≥500ms) | 切换到绿色呼吸灯模式 |

## 学习要点

- **FreeRTOS多任务**：`xTaskCreate()` 创建3个独立任务
- **互斥量**：`xSemaphoreCreateMutex()` 保护共享变量
- **GPIO输入**：`gpio_config()` 配置上拉输入
- **按键消抖**：20ms延时过滤抖动
- **长按/短按**：通过 `esp_timer_get_time()` 计算按压时长

## 修改学号姓名

在 `main.c` 顶部修改：

```c
#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"
```
