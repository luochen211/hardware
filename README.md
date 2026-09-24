# 硬件入门与实验

这里先用概念图认识联网硬件的三个层面，再通过实验理解机器人、单片机、传感器和执行器如何协作。

![联网硬件三层概念图：感知层、网络层、应用层，以及数据上报和控制命令的双向流动](docs/images/iot-three-layer-architecture.png)

## 硬件领域导览

- [硬件宏观认知思维导图](docs/硬件宏观认知思维导图.md)：从感知层、网络层和应用层理解硬件系统。

**感知层**通过传感器接触真实物理状态，并由单片机控制本地动作；**网络层**传送设备选择上报的数据和下发的命令；**应用层**查看收到的信息，再通过开放接口发出命令。电脑连上设备，不等于能直接看到所有电气和机械状态。

例如，手机收到 ESP32 上报的土壤湿度值，只能说明设备发送了这个读数；要确认传感器供电和接线，还需要相应的诊断信息或实际测量。

## 八个实验的学习路线

![ESP32 八阶段学习规划图：彩灯输出、按键与任务、环境感知、执行器控制、Wi-Fi 与 TCP、蓝牙通信、MQTT 消息、综合应用；第 3、4、5、6、8 阶段包含可选分支](docs/images/esp32-learning-roadmap.png)

现在有 8 个阶段、13 个 ESP32 工程。第 3、4、5、6、8 阶段各有不同板卡或通信方案的分支，按需选择，不必全部烧录。另有一份[早期 WS2812 练习副本](experiments/esp32-ws2812-lab/README.md)，用于记录最初的灯光实验与踩坑过程。

| 阶段 | 工程 | 主要内容 |
| --- | --- | --- |
| 1 · 灯光输出 | [WS2812 彩灯](experiments/exp1_blink/README.md) | SPI 驱动 RGB 灯，练习状态输出 |
| 2 · 按键与任务 | [GPIO + FreeRTOS](experiments/exp2_gpio_freertos/README.md) | 按键消抖、长短按、多任务协作 |
| 3 · 感知环境 | [智能家居板](experiments/exp3a_home/README.md) / [智慧农业板](experiments/exp3b_farm/README.md) | DHT11 与烟雾，或土壤、光照与雨滴；OLED 显示 |
| 4 · 控制执行器 | [智能家居板](experiments/exp4a_home/README.md) / [智慧农业板](experiments/exp4b_farm/README.md) | 蜂鸣器、步进电机、彩灯、继电器，或风扇开关与彩灯 |
| 5 · 接入局域网 | [固定配置 Wi-Fi + TCP](experiments/exp5a_hardcode/README.md) / [SmartConfig + TCP](experiments/exp5b_smartconfig/README.md) | 比较写入配置与手机配网，向电脑发送示例 JSON |
| 6 · 蓝牙 | [BLE GATT 控灯](experiments/exp6a_gatt/README.md) / [BLE 配网](experiments/exp6b_ble_prov/README.md) | 手机控制 WS2812，或通过蓝牙配置 Wi-Fi |
| 7 · 消息通信 | [MQTT](experiments/exp7_mqtt/README.md) | 上报模拟温湿度数据，订阅灯光控制命令 |
| 8 · 综合连接 | [MQTT 智慧农业演示](experiments/exp8a_mqtt/README.md) / [RainMaker 智能家居](experiments/exp8b_rainmaker/README.md) | 一个演示 MQTT 远程控制与模拟数据，另一个读取 DHT11、烟雾并接入 RainMaker |

这些目录包含源码和操作说明，部分还留有本机构建产物；**工程存在或编译成功，不等于已经在实训箱上完成接线、烧录和逐项验收**。第 5、7、8A 阶段的温湿度数据为模拟值；第 8B 阶段会尝试读取真实 DHT11 和烟雾传感器，读数仍需实物核验。实验前请按所用板卡确认引脚、电源与外设型号。

## 芯片知识

芯片设计需要同时满足功能、版图和性能约束。下图展示了流片前的验证关卡，以及流片后发现错误为什么会带来更长的返工周期。更多解释见[芯片从设计到流片](docs/芯片从设计到流片.md)。

![流片难点：功能、版图、速度功耗面积需要在流片前验证，流片后发现错误会增加成本和时间](docs/images/why-tapeout-is-hard.png)

## 行业与职业

- [硬件与软件：入行门槛和经验积累](docs/硬件与软件职业路径对比.md)：用对照图解释实体验证、迭代速度与经验价值的差异，并说明“35 岁毕业”不是软件的技术规律。

![硬件与软件职业路径对比：入行条件、经验积累和各自挑战](docs/images/hardware-vs-software-careers.png)

## AI 时代的投入结构

- [硬件、软件与 AI：投入发生在哪里](docs/硬件软件与AI时代的投入结构.md)：对比原型、规模部署和持续运营的成本，说明为什么硬件前置投入明显，以及 AI 服务怎样依赖算力、数据中心和能源。

![硬件、软件和 AI 服务在不同阶段的投入结构对比](docs/images/investment-hardware-software-ai.png)

## 复盘

- [实验过程与踩坑记录](docs/esp32-ws2812-lab-retrospective.md)

早期 WS2812 练习副本只控制彩灯，没有读取土壤湿度。土壤、光照和雨滴 ADC 采集见[实验 3B](experiments/exp3b_farm/README.md)；相关读数是否准确，仍需结合接线、校准和实物测量确认。
