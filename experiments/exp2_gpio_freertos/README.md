# 实验2：FreeRTOS多任务 + GPIO + 按键检测

## 功能

| 任务 | 功能 |
|------|------|
| blink_task | LED(IO2)闪烁，支持4种模式（闪烁/呼吸灯/常亮/常灭）|
| button_task | K1_C(IO22)按键检测，消抖20ms，长按/短按判断 |
| print_task | 每5秒串口打印按键计数和LED模式 |

## 引脚

| 外设 | 引脚 | 说明 |
|------|------|------|
| LED | IO2 | 板载蓝色LED |
| K1_C 拨动开关 | IO22 | 上拉输入，按下接地 |
| K1_D 拨动开关 | IO21 | 上拉输入，按下接地（备用）|

> **注意**：K1_B的IO2与LED冲突，所以按键改用K1_C(IO22)。

## 编译运行

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## 操作说明

| 操作 | 效果 |
|------|------|
| 短按 K1_C | 切换LED常亮/常灭 |
| 长按 K1_C (≥500ms) | 切换呼吸灯模式 |

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
