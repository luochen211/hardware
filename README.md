# 硬件学习：实操与认知

这个仓库有两个入口。README 负责总览；实验步骤留在工程中，连续的概念解释从认知篇开始。

| 从动手开始 | 从理解开始 |
| --- | --- |
| [进入实操路线](#第一部分实操)：8 个阶段、13 个 ESP32 工程，从彩灯走到联网应用。 | [阅读硬件认知篇](docs/硬件认知篇.md)：先比较硬件与软件的工作和就业，再学习硬件常识。 |

## 第一部分：实操

### ESP32 八阶段学习规划

![ESP32 八阶段学习规划图：从彩灯、按键、传感器和执行器，到 Wi-Fi、蓝牙、MQTT 与综合应用；部分阶段有家居、农业或通信方案分支](docs/images/esp32-learning-roadmap.png)

目前收录 8 个阶段、13 个工程。第 3、4、5、6、8 阶段有不同板卡或通信方案的分支，可以按手头设备和学习目标选择。

| 阶段 | 工程 | 学习内容 |
| --- | --- | --- |
| 1 · 彩灯输出 | [WS2812 彩灯](experiments/exp1_blink/README.md) | SPI 驱动 RGB 灯，观察串口日志 |
| 2 · 按键与任务 | [GPIO + FreeRTOS](experiments/exp2_gpio_freertos/README.md) | 按键消抖、长短按与多任务 |
| 3 · 环境感知 | [智能家居板](experiments/exp3a_home/README.md) / [智慧农业板](experiments/exp3b_farm/README.md) | 温湿度与烟雾，或土壤、光照与雨滴采集；OLED 显示 |
| 4 · 执行器控制 | [智能家居板](experiments/exp4a_home/README.md) / [智慧农业板](experiments/exp4b_farm/README.md) | 蜂鸣器、步进电机、继电器，或风扇开关与彩灯 |
| 5 · Wi-Fi 与 TCP | [固定配置](experiments/exp5a_hardcode/README.md) / [SmartConfig 配网](experiments/exp5b_smartconfig/README.md) | 连接局域网，向电脑发送示例 JSON |
| 6 · 蓝牙通信 | [BLE GATT 控灯](experiments/exp6a_gatt/README.md) / [BLE 配网](experiments/exp6b_ble_prov/README.md) | 手机控制彩灯，或通过蓝牙配置 Wi-Fi |
| 7 · MQTT 消息 | [MQTT 工程](experiments/exp7_mqtt/README.md) | 上报模拟温湿度，订阅控制命令 |
| 8 · 综合应用 | [MQTT 演示](experiments/exp8a_mqtt/README.md) / [RainMaker 智能家居](experiments/exp8b_rainmaker/README.md) | 远程控制、数据显示与多外设集成 |

这些工程有源码和操作说明；部分目录留有本机构建产物。**构建成功不代表已经完成接线、烧录和实物验收。**第 5、7、8A 阶段的温湿度数据为模拟值；第 8B 阶段尝试读取真实 DHT11 与烟雾传感器，读数仍需实物核验。

实物入口：[实训箱照片与工程对照](docs/实训箱硬件与工程对照.md)。照片保留板上的原有丝印，对照表说明每块板对应哪些工程。

实验记录：[早期 WS2812 练习副本](experiments/esp32-ws2812-lab/README.md) · [实验过程与踩坑复盘](docs/esp32-ws2812-lab-retrospective.md)。早期副本只控制彩灯；土壤、光照和雨滴采集见[实验 3B](experiments/exp3b_farm/README.md)。

## 第二部分：认知

从一篇连续的[硬件认知篇](docs/硬件认知篇.md)开始：第一部分比较硬件与软件的工作对象、反馈周期、入门和经验积累；第二部分从硬件常识和设备工作过程，逐步讲到选用现成芯片、模组与开发板。文末提供专题深读入口。

![硬件与软件在入行条件、经验积累和职业挑战上的对比](docs/images/hardware-vs-software-careers.png)
