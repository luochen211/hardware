# 实验6：BLE GATT Server

## 功能

ESP32 作为 BLE GATT Server，手机通过 nRF Connect App 连接后可控制 LED。

| 项目 | 值 |
|------|------|
| 设备名称 | `IoT_Student` |
| Service UUID | `0x00FF` |
| Characteristic UUID | `0xFF01` |
| 权限 | READ + WRITE |
| 引脚 | WS2812 数据线 = GPIO14（SPI） |

## 通信协议

| 手机写入 | ESP32 动作 |
|---------|-----------|
| `ON` | LED 亮 |
| `OFF` | LED 灭 |

手机读取特征值时返回字符串 `"Hello BLE"`。

## 编译运行

```bash
cd exp6a_gatt
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

启动后串口输出：
```
[BLE_GATTS] advertising started, waiting for connection...
[BLE_GATTS] Use nRF Connect to scan and connect to "IoT_Student"
```

## 手机端测试步骤

1. **安装 App**：在手机应用商店搜索安装 **nRF Connect**（Nordic Semiconductor）
   - Android: Google Play 搜索 "nRF Connect"
   - iOS: App Store 搜索 "nRF Connect"

2. **扫描设备**：打开 App → 点击 SCAN → 找到 `IoT_Student`

3. **连接设备**：点击 CONNECT

4. **查看服务**：展开 `Unknown Service 0x00FF`

5. **读取特征值**：点击 `0xFF01` 旁的下拉箭头 → 点击读取图标（向上箭头）
   - 应显示返回值：`Hello BLE`

6. **写入控制命令**：
   - 点击写入图标（向下箭头）
   - 选择 Text 模式
   - 输入 `ON` → 发送 → LED 应亮
   - 输入 `OFF` → 发送 → LED 应灭

## 扩展方向

- 添加 NOTIFY 特征，主动推送传感器数据到手机
- 添加多个特征值（LED控制 + 温湿度数据 + 电机控制）
- 添加 BLE 配网功能（手机通过 BLE 发送 WiFi 账号密码）
