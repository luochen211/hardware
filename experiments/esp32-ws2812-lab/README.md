# ESP32 WS2812 RGB 灯实验

**分类：硬件 / ESP32 / LED**

此目录保存老师提供的 `exp1_blink` 实验工作副本。为便于公开分享，源码中的学生信息使用 `YOUR_STUDENT_ID` 和 `YOUR_NAME` 占位符；使用者可在本地替换。老师的原始资料不在本仓库内；引用的说明副本见 `references/README-teacher.md`。

## 当前功能

- 初始化 WS2812 SPI 驱动
- 循环显示红、绿、蓝、白、熄灭状态
- 串口输出学生信息和颜色状态

此实验不读取土壤湿度传感器，也不输出湿度数据。

## macOS 编译

当前机器上的 ESP-IDF 安装在 EIM 路径。不要 source 旧的 `activate_idf_v6.1.sh`，它会把 `IDF_PATH` 设到已失效的路径。打开新终端后运行：

```zsh
cd "$HOME/Desktop/hardware/experiments/esp32-ws2812-lab"
export IDF_PATH="$HOME/.espressif/eim-install/v6.1/esp-idf"
export IDF_TOOLS_PATH="$HOME/.espressif/tools"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/tools/python/v6.1/venv"
export ESP_IDF_VERSION=6.1
export IDF_VERSION=6.1.0
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" set-target esp32
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" build
```

## 烧录与串口查看

确认开发板连接后，先找实际串口名：

```zsh
ls /dev/cu.*
```

把下方端口替换成设备当前对应的串口，然后执行：

```zsh
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" -p /dev/cu.usbserial-1340 flash monitor
```

串口监视器会占用端口；同一时刻不要再开第二个监视器。退出监视器按 `Ctrl+]`。

## 引脚核对

当前源码实际宏配置为 `WS2812_MOSI_PIN 14`。源码头部注释和教师说明副本还提到 GPIO21 / GPIO23，资料之间不一致。接线前应以实训箱原理图和实际板卡为准；不要只根据注释改引脚。
