/**
 * ============================================================
 *  实验七：MQTT 客户端 + SmartConfig配网
 * ============================================================
 *  功能：
 *    1. SmartConfig配网（首次用手机ESP-TOUCH APP，之后NVS自动记忆）
 *    2. 连接公共MQTT Broker（broker.emqx.io:1883）
 *    3. 每5秒发布模拟传感器数据JSON到 /school/{学号}/sensor
 *    4. 订阅 /school/{学号}/cmd，接收 led_on / led_off 命令控制WS2812
 *    5. OLED显示MQTT连接状态和IP地址
 *
 *  编译运行：
 *    idf.py set-target esp32
 *    idf.py build flash monitor
 *
 *  测试：
 *    手机MQTT Dashboard或PC MQTTX连接broker.emqx.io:1883
 *    订阅 /school/学号/sensor → 看传感器数据
 *    发布 led_on 到 /school/学号/cmd → LED亮
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
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"
#include "esp_smartconfig.h"

static const char *TAG = "EXP7";

/* ============================ 配置区 ============================ */
#define MQTT_BROKER_URI  "mqtt://broker.emqx.io:1883"
#define STUDENT_ID       "2024001"
#define TOPIC_SENSOR     "/school/" STUDENT_ID "/sensor"
#define TOPIC_CMD        "/school/" STUDENT_ID "/cmd"
#define PUBLISH_PERIOD_MS 5000

/* ============================ 引脚定义 ============================ */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000
#define BUTTON_K1C         22

#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C

/* ============================ WS2812 SPI 驱动 ============================ */
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

/* ============================ I2C OLED (SSD1306) ============================ */
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

/* ============================ WiFi + SmartConfig ============================ */
#define WIFI_CONNECTED_BIT  BIT0
#define ESPTOUCH_DONE_BIT   BIT1
static EventGroupHandle_t s_wifi_event_group;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

#define NVS_NAMESPACE   "wifi_cred"
#define MAX_SSID_LEN    32
#define MAX_PASS_LEN    64

static void save_wifi_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "ssid", ssid, strlen(ssid) + 1);
        nvs_set_blob(handle, "pass", password, strlen(password) + 1);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool load_wifi_from_nvs(char *ssid, char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t sl = MAX_SSID_LEN, pl = MAX_PASS_LEN;
    esp_err_t r1 = nvs_get_blob(handle, "ssid", ssid, &sl);
    esp_err_t r2 = nvs_get_blob(handle, "pass", password, &pl);
    nvs_close(handle);
    return (r1 == ESP_OK && r2 == ESP_OK);
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ws2812_set_color(50, 0, 0);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi连接成功! IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        ws2812_set_color(0, 50, 0);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        char ssid[MAX_SSID_LEN] = {0};
        char pass[MAX_PASS_LEN] = {0};
        strncpy(ssid, (char *)evt->ssid, sizeof(ssid) - 1);
        strncpy(pass, (char *)evt->password, sizeof(pass) - 1);
        ESP_LOGI(TAG, "SmartConfig收到: SSID=%s", ssid);
        save_wifi_to_nvs(ssid, pass);
        wifi_config_t cfg = {0};
        memcpy(cfg.sta.ssid, evt->ssid, sizeof(cfg.sta.ssid));
        memcpy(cfg.sta.password, evt->password, sizeof(cfg.sta.password));
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void smartconfig_task(void *arg)
{
    oled_show_text(1, "Use ESP-TOUCH");
    oled_show_text(2, "to configure WiFi");
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
    esp_smartconfig_start(&cfg);
    xEventGroupWaitBits(s_wifi_event_group, ESPTOUCH_DONE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    esp_smartconfig_stop();
    vTaskDelete(NULL);
}

static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 先尝试NVS已保存的WiFi */
    char ssid[MAX_SSID_LEN] = {0};
    char pass[MAX_PASS_LEN] = {0};
    if (load_wifi_from_nvs(ssid, pass)) {
        ESP_LOGI(TAG, "使用已保存WiFi: %s", ssid);
        wifi_config_t wc = {0};
        strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
        strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_connect();
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    }

    /* 没连上就启动SmartConfig */
    if (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)) {
        ESP_LOGI(TAG, "启动SmartConfig配网...");
        oled_show_text(0, "SmartConfig");
        xTaskCreate(smartconfig_task, "sc", 4096, NULL, 3, NULL);
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    }
}

/* ============================ MQTT ============================ */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT已连接");
        oled_show_text(3, "MQTT Connected");
        esp_mqtt_client_subscribe(event->client, TOPIC_CMD, 0);
        ESP_LOGI(TAG, "已订阅: %s", TOPIC_CMD);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT断开");
        oled_show_text(3, "MQTT Disc..");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "收到: %.*s = %.*s", event->topic_len, event->topic, event->data_len, event->data);
        char buf[128];
        int len = event->data_len < (int)sizeof(buf) - 1 ? event->data_len : (int)sizeof(buf) - 1;
        memcpy(buf, event->data, len);
        buf[len] = '\0';
        if (strstr(buf, "led_on")) {
            ws2812_set_color(80, 0, 0);
            ESP_LOGI(TAG, "LED ON");
        } else if (strstr(buf, "led_off")) {
            ws2812_set_color(0, 0, 0);
            ESP_LOGI(TAG, "LED OFF");
        }
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

/* ============================ 发布任务 ============================ */
static void publish_task(void *arg)
{
    char payload[128];
    int seq = 0;
    srand((unsigned int)xTaskGetTickCount());
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
        if (s_mqtt_client == NULL) continue;
        int temp = 20 + rand() % 16;
        int humi = 40 + rand() % 41;
        snprintf(payload, sizeof(payload),
                 "{\"temp\":%d,\"humi\":%d,\"seq\":%d}", temp, humi, seq);
        esp_mqtt_client_publish(s_mqtt_client, TOPIC_SENSOR, payload, 0, 0, 0);
        ESP_LOGI(TAG, "发布 #%d: %s", seq, payload);
        seq++;
    }
}

/* ============================ app_main ============================ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== 实验7: MQTT + SmartConfig ===");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* LED + OLED */
    ws2812_init();
    ws2812_set_color(50, 0, 0);
    oled_init();
    oled_show_text(0, "MQTT Exp7");

    /* 长按K1_C清除配网 */
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << BUTTON_K1C),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);
    if (gpio_get_level(BUTTON_K1C) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (gpio_get_level(BUTTON_K1C) == 0) {
            ESP_LOGI(TAG, "清除WiFi凭据");
            esp_wifi_restore();
            nvs_flash_erase();
            esp_restart();
        }
    }

    /* WiFi */
    wifi_init();

    /* OLED显示WiFi状态 */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        oled_show_text(0, "WiFi OK!");
        oled_show_text(1, (char *)ap_info.ssid);
    }

    /* MQTT */
    ESP_LOGI(TAG, "连接MQTT Broker: %s", MQTT_BROKER_URI);
    oled_show_text(2, "Connecting MQTT");
    mqtt_start();

    /* 发布任务 */
    xTaskCreate(publish_task, "publish", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "系统就绪");
    ESP_LOGI(TAG, "订阅: %s", TOPIC_SENSOR);
    ESP_LOGI(TAG, "控制: %s (led_on/led_off)", TOPIC_CMD);
}
