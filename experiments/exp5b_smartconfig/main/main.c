/**
 * ============================================================
 *  实验五B：SmartConfig配网 + TCP 客户端通信
 * ============================================================
 *  功能：
 *    1. SmartConfig配网（手机ESP-TOUCH APP，首次配网后自动记忆）
 *    2. 创建 TCP 客户端 socket，连接 PC 服务器
 *    3. 循环发送 JSON 格式传感器数据
 *    4. 接收服务器回显
 *    5. 断线自动重连
 *    6. OLED显示连接状态和传感器数据
 *
 *  配网方法：
 *    首次：手机安装ESP-TOUCH APP → 输入WiFi密码 → 配网
 *    之后：自动连接（NVS保存了凭据，无需再配网）
 *
 *  APP下载：https://github.com/EspressifApp/esp-touch
 * ============================================================
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"

#include "wifi_connect.h"

static const char *TAG = "exp5";

/* ====== TCP 服务器配置 ====== */
#define TCP_SERVER_IP     "192.168.110.222"   /* PC 服务器 IP */
#define TCP_SERVER_PORT   8080               /* 服务器端口 */
#define TCP_BUF_SIZE      256                /* 收发缓冲区大小 */
#define SEND_INTERVAL_S   3                  /* 发送间隔（秒） */
/* ============================ */

/* ====== OLED 引脚 ====== */
#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C

/* ============================ OLED 驱动 ============================ */
static esp_err_t oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x00, true);
    i2c_master_write_byte(h, cmd, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, h, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(h);
    return ret;
}
static esp_err_t oled_write_data(uint8_t data)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x40, true);
    i2c_master_write_byte(h, data, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, h, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(h);
    return ret;
}
static void oled_clear(void)
{
    for (uint8_t p = 0; p < 8; p++) {
        oled_write_cmd(0xB0 + p); oled_write_cmd(0x00); oled_write_cmd(0x10);
        for (int c = 0; c < 128; c++) oled_write_data(0x00);
    }
}
static const uint8_t font5x8[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x2f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},{0x62,0x64,0x08,0x13,0x23},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1c,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1c,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x00,0xA0,0x60,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x55,0x2A,0x55,0x2A,0x55},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x18,0xA4,0xA4,0xA4,0x7C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x40,0x80,0x84,0x7D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0xFC,0x24,0x24,0x24,0x18},
    {0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x1C,0xA0,0xA0,0xA0,0x7C},
    {0x44,0x64,0x54,0x4C,0x44},{0x08,0x36,0x41,0x00,0x00},{0x00,0x00,0x77,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02},
};
static void oled_show_text(int line, const char *text)
{
    if (line < 0 || line > 7) return;
    oled_write_cmd(0xB0 + line); oled_write_cmd(0x00); oled_write_cmd(0x10);
    for (int i = 0; i < 21; i++) { for (int c = 0; c < 5; c++) oled_write_data(0x00); oled_write_data(0x00); }
    oled_write_cmd(0xB0 + line); oled_write_cmd(0x00); oled_write_cmd(0x10);
    for (int i = 0; text[i] != '\0' && i < 21; i++) {
        char c = text[i]; if (c < 32 || c > 126) c = ' '; uint8_t idx = c - 32;
        for (int col = 0; col < 5; col++) oled_write_data(font5x8[idx][col]);
        oled_write_data(0x00);
    }
}
static void oled_init(void)
{
    i2c_config_t conf = { .mode = I2C_MODE_MASTER, .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO, .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = I2C_MASTER_FREQ_HZ };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "OLED: SCL=GPIO%d SDA=GPIO%d", I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO);
    const uint8_t cmds[] = {0xAE,0x20,0x00,0x40,0x81,0xCF,0xA1,0xA8,0x3F,0xC8,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,0xDB,0x40,0x8D,0x14,0xA4,0xA6,0xAF};
    for (int i = 0; i < sizeof(cmds); i++) oled_write_cmd(cmds[i]);
    oled_clear();
}

/**
 * @brief 模拟读取温湿度传感器数据
 */
static void read_sensor_data(int *temp, int *humi)
{
    static int s_temp = 25;
    static int s_humi = 60;
    s_temp += (esp_timer_get_time() % 3) - 1;
    s_humi += (esp_timer_get_time() % 3) - 1;
    if (s_temp < 15)  s_temp = 15;
    if (s_temp > 35)  s_temp = 35;
    if (s_humi < 30)  s_humi = 30;
    if (s_humi > 80)  s_humi = 80;
    *temp = s_temp;
    *humi = s_humi;
}

/**
 * @brief TCP 客户端任务
 */
static void tcp_client_task(void *arg)
{
    char tx_buf[TCP_BUF_SIZE];
    char rx_buf[TCP_BUF_SIZE];
    struct sockaddr_in server_addr;
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(TCP_SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(TCP_SERVER_IP);

    while (1) {
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "创建 socket 失败: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        oled_show_text(3, "TCP Connecting..");

        int err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "连接服务器失败: errno=%d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        ESP_LOGI(TAG, "已连接 TCP 服务器 %s:%d", TCP_SERVER_IP, TCP_SERVER_PORT);
        oled_show_text(3, "TCP Connected!");

        while (1) {
            int temp = 0, humi = 0;
            read_sensor_data(&temp, &humi);
            int len = snprintf(tx_buf, sizeof(tx_buf),
                               "{\"temp\":%d,\"humi\":%d}", temp, humi);
            int sent = send(sock, tx_buf, len, 0);
            if (sent < 0) {
                ESP_LOGE(TAG, "发送失败: errno=%d", errno);
                break;
            }
            ESP_LOGI(TAG, "发送 [%d bytes]: %s", sent, tx_buf);

            /* OLED显示传感器数据 */
            char oled_buf[22];
            snprintf(oled_buf, sizeof(oled_buf), "T:%dC H:%d%%", temp, humi);
            oled_show_text(2, oled_buf);

            memset(rx_buf, 0, sizeof(rx_buf));
            int rlen = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
            if (rlen < 0) {
                ESP_LOGE(TAG, "接收失败: errno=%d", errno);
                break;
            } else if (rlen == 0) {
                ESP_LOGW(TAG, "服务器关闭了连接");
                break;
            }
            rx_buf[rlen] = '\0';
            ESP_LOGI(TAG, "收到回显 [%d bytes]: %s", rlen, rx_buf);
            vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_S * 1000));
        }

        if (sock >= 0) close(sock);
        ESP_LOGW(TAG, "连接断开，5秒后重连...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* IP获取成功回调：显示IP到OLED */
static void on_ip_ready(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    char ip_buf[20];
    snprintf(ip_buf, sizeof(ip_buf), "IP:%d.%d.%d.%d", (int)a, (int)b, (int)c, (int)d);
    oled_show_text(1, ip_buf);
}

/* ============================================================
 *  主程序
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== 实验五B：SmartConfig + TCP 客户端 ===");

    /* 1. NVS 初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. OLED初始化 */
    oled_init();
    oled_show_text(0, "Exp5B SmartConfig");
    oled_show_text(1, "Connecting WiFi");

    /* 3. 设置IP回调 + SmartConfig配网 */
    g_on_ip_ready = on_ip_ready;
    wifi_init_sta();
    /* IP由回调函数on_ip_ready在IP事件中自动显示到OLED */

    /* 4. 启动 TCP 客户端任务 */
    xTaskCreate(tcp_client_task, "tcp_client", 8192, NULL, 5, NULL);
}
