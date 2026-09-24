/**
 * ============================================================
 *  实验6B：BLE Provisioning 配网
 * ============================================================
 *  功能：
 *    1. 首次：手机APP「ESP BLE Prov」通过蓝牙配网（输入WiFi密码）
 *    2. 配网成功后自动连接WiFi
 *    3. WiFi凭据保存在NVS，下次上电自动连接
 *    4. 长按K1_C(GPIO22) 5秒可清除配网信息
 *    5. LED状态指示：红灯=未连接，绿灯=已连接
 *
 *  注意：本实验只做配网，BLE GATT控制LED见实验6A
 *
 *  APP：ESP BLE Prov（Espressif官方，Security Version 0）
 *
 *  编译运行：
 *    idf.py set-target esp32
 *    idf.py build flash monitor
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"

/* BLE Provisioning */
#include "network_provisioning/scheme_ble.h"
#include "network_provisioning/manager.h"

static const char *TAG = "EXP6B";
static const char *PROV_SERVICE_NAME = "PROV_IOT_Student";

/* K1_C 拨动开关 */
#define BUTTON_K1C       22

/* ============== I2C OLED (SSD1306) ============== */
#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C

/* ============== WS2812 SPI 驱动 ============== */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000

static spi_device_handle_t s_spi;

static void ws2812_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = WS2812_MOSI_PIN,
        .miso_io_num     = -1,
        .sclk_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(WS2812_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = WS2812_SPI_CLK,
        .mode           = 0,
        .spics_io_num   = -1,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(WS2812_SPI_HOST, &devcfg, &s_spi));
}

static void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = { g, r, b };
    uint8_t spi_buf[24];
    int idx = 0;
    for (int c = 0; c < 3; c++)
        for (int bit = 7; bit >= 0; bit--)
            spi_buf[idx++] = (grb[c] >> bit) & 1 ? 0xFC : 0xE0;
    spi_transaction_t t = { .length = 24 * 8, .tx_buffer = spi_buf };
    spi_device_polling_transmit(s_spi, &t);
    esp_rom_delay_us(60);
}

/* ============== I2C OLED (SSD1306) ============== */
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
    for (uint8_t page = 0; page < 8; page++) {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);
        for (int col = 0; col < 128; col++)
            oled_write_data(0x00);
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
    oled_write_cmd(0xB0 + line);
    oled_write_cmd(0x00);
    oled_write_cmd(0x10);
    for (int i = 0; i < 21; i++) {
        for (int col = 0; col < 5; col++)
            oled_write_data(0x00);
        oled_write_data(0x00);
    }
    oled_write_cmd(0xB0 + line);
    oled_write_cmd(0x00);
    oled_write_cmd(0x10);
    for (int i = 0; text[i] != '\0' && i < 21; i++) {
        char c = text[i];
        if (c < 32 || c > 126) c = ' ';
        uint8_t idx = c - 32;
        for (int col = 0; col < 5; col++)
            oled_write_data(font5x8[idx][col]);
        oled_write_data(0x00);
    }
}

static void oled_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    const uint8_t cmds[] = {0xAE,0x20,0x00,0x40,0x81,0xCF,0xA1,0xA8,0x3F,
                            0xC8,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,
                            0xDB,0x40,0x8D,0x14,0xA4,0xA6,0xAF};
    for (int i = 0; i < sizeof(cmds); i++)
        oled_write_cmd(cmds[i]);
    oled_clear();
}

/* ============== WiFi事件 ============== */
#define WIFI_CONNECTED_BIT  BIT0
static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi断开，重连...");
        ws2812_set_color(50, 0, 0);  /* 红灯=未连接 */
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi已连接! IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        ws2812_set_color(0, 50, 0);  /* 绿灯=已连接 */
        /* OLED显示配网成功+IP */
        oled_show_text(0, "Provisioned!");
        oled_show_text(1, "WiFi Connected");
        char ip_str[20];
        snprintf(ip_str, sizeof(ip_str), "IP:" IPSTR, IP2STR(&evt->ip_info.ip));
        oled_show_text(2, ip_str);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "[配网] Provisioning启动");
            ws2812_set_color(50, 25, 0);  /* 黄灯=配网中 */
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "[配网] 收到WiFi信息: SSID=%s", (char *)cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGE(TAG, "[配网] WiFi凭据错误");
            network_prov_mgr_reset_wifi_sm_state_on_failure();
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "[配网] WiFi凭据验证成功!");
            break;
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "[配网] 完成");
            network_prov_mgr_deinit();
            break;
        default:
            break;
        }
    }
}

/* ============== BLE Provisioning 配网 ============== */
static void wifi_prov_init(void)
{
    /* 检查是否已配过网（直接读NVS） */
    wifi_config_t wifi_cfg = {0};
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    if (ret == ESP_OK && strlen((char *)wifi_cfg.sta.ssid) > 0) {
        ESP_LOGI(TAG, "已配网: SSID=%s，直接连接WiFi", (char *)wifi_cfg.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        return;
    }

    /* --- 未配网，启动BLE Provisioning --- */
    ESP_LOGI(TAG, "未配网，启动BLE Provisioning...");

    network_prov_mgr_config_t cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(network_prov_mgr_init(cfg));

    /* 启动配网 */
    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_0, NULL, PROV_SERVICE_NAME, NULL));

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BLE Provisioning 已启动");
    ESP_LOGI(TAG, "  1. 手机安装「ESP BLE Prov」APP");
    ESP_LOGI(TAG, "  2. APP设置里选 Security Version 0");
    ESP_LOGI(TAG, "  3. 扫描找到 \"%s\"", PROV_SERVICE_NAME);
    ESP_LOGI(TAG, "  4. 选择WiFi，输入密码");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}

/* ============== 主函数 ============== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== 实验6B: BLE Provisioning 配网 ===");

    /* 1. NVS初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. WS2812 + OLED初始化 */
    ws2812_init();
    ws2812_set_color(50, 0, 0);  /* 红灯=未连接 */
    oled_init();
    oled_show_text(0, "BLE Prov Ready");
    oled_show_text(1, "Use ESP BLE Prov");
    oled_show_text(2, "Waiting...");

    /* 3. 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 4. 初始化网络和事件循环 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* 5. WiFi初始化 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 6. 注册事件 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    /* 7. 长按K1_C(5秒)清除配网信息 */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_K1C),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_conf);

    if (gpio_get_level(BUTTON_K1C) == 0) {
        ESP_LOGI(TAG, "检测到K1_C按下，5秒后清除配网...");
        ws2812_set_color(50, 25, 0);  /* 黄灯=等待确认 */
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (gpio_get_level(BUTTON_K1C) == 0) {
            ESP_LOGI(TAG, "清除WiFi凭据，重启进入配网模式");
            ws2812_set_color(50, 0, 0);  /* 红灯=清除中 */
            esp_wifi_restore();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        ESP_LOGI(TAG, "K1_C已松开，取消清除");
        ws2812_set_color(50, 0, 0);  /* 恢复红灯 */
    }

    /* 8. BLE Provisioning配网 */
    wifi_prov_init();

    /* 9. 等待WiFi连接 */
    ESP_LOGI(TAG, "等待WiFi连接...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "WiFi已连接，配网完成！");
    ESP_LOGI(TAG, "LED状态: 绿灯=WiFi已连接");
    ESP_LOGI(TAG, "============================");

    /* 主循环 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
