/*
 * 智能农业监控系统（ESP RainMaker）
 * 基于esp-rainmaker/examples/switch改造
 *
 * 功能：温湿度上报 + LED控制 + 风扇控制 + OLED显示
 *
 * 硬件：实训箱ESP32-WROOM-32E-N8
 *   WS2812 → GPIO14 (SPI3)
 *   OLED   → SCL=GPIO19, SDA=GPIO18
 *   风扇   → GPIO17
 */

#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_rom_sys.h>
#include <stdlib.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>

#include <app_network.h>
#include <app_insights.h>

#include "app_priv.h"

static const char *TAG = "SMART_HOME";

/* ============================ 引脚定义 ============================ */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000
#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C
#define FAN_GPIO           17
#define DHT11_PIN          GPIO_NUM_5
#define BUZZER_GPIO        13
#define MOTOR_IN1          26
#define MOTOR_IN2          25
#define MOTOR_IN3          33
#define MOTOR_IN4          32
#define SMOKE_ADC_CH       ADC_CHANNEL_7  /* GPIO35 */

/* ============================ DHT11 驱动 ============================ */
static int dht11_read(int *humidity, int *temperature)
{
    uint8_t buf[5] = {0};
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT11_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(DHT11_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT11_PIN, 1);
    portDISABLE_INTERRUPTS();
    esp_rom_delay_us(30);
    int retry = 0;
    while (gpio_get_level(DHT11_PIN) && retry < 200) { esp_rom_delay_us(1); retry++; }
    if (retry >= 200) { portENABLE_INTERRUPTS(); return -1; }
    retry = 0;
    while ((!gpio_get_level(DHT11_PIN)) && retry < 200) { esp_rom_delay_us(1); retry++; }
    if (retry >= 200) { portENABLE_INTERRUPTS(); return -1; }
    for (int byte_idx = 0; byte_idx < 5; byte_idx++) {
        uint8_t dat = 0;
        for (int bit = 0; bit < 8; bit++) {
            dat <<= 1;
            retry = 0;
            while (gpio_get_level(DHT11_PIN) && retry < 200) { esp_rom_delay_us(1); retry++; }
            retry = 0;
            while ((!gpio_get_level(DHT11_PIN)) && retry < 200) { esp_rom_delay_us(1); retry++; }
            esp_rom_delay_us(40);
            if (gpio_get_level(DHT11_PIN)) dat |= 1;
        }
        buf[byte_idx] = dat;
    }
    portENABLE_INTERRUPTS();
    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) return -2;
    *humidity = buf[0];
    *temperature = buf[2];
    return 0;
}

/* ============================ 步进电机 28BYJ48 ============================ */
static const int motor_pins[4] = {MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4};
/* 8拍驱动序列 */
static const uint8_t motor_seq[8][4] = {
    {1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},
    {0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1},
};

static void motor_init(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(motor_pins[i]);
        gpio_set_direction(motor_pins[i], GPIO_MODE_OUTPUT);
    }
}

static void motor_step(int steps)
{
    static int phase = 0;
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 4; i++)
            gpio_set_level(motor_pins[i], motor_seq[phase][i]);
        phase = (phase + 1) % 8;
        esp_rom_delay_us(3000);
    }
}

/* ============================ 蜂鸣器 ============================ */
static void buzzer_init(void)
{
    gpio_reset_pin(BUZZER_GPIO);
    gpio_set_direction(BUZZER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_GPIO, 0);
}

/* 蜂鸣器短响（非阻塞：用vTaskDelay） */
static void buzzer_beep(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_set_level(BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(BUZZER_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ============================ 烟雾传感器 ADC (新API) ============================ */
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

static void smoke_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, SMOKE_ADC_CH, &config));
}

static int smoke_read(void)
{
    int val = 0;
    adc_oneshot_read(s_adc1_handle, SMOKE_ADC_CH, &val);
    return val;
}

/* ============================ WS2812 SPI 驱动 ============================ */
static spi_device_handle_t s_spi;
static void ws2812_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = WS2812_MOSI_PIN, .miso_io_num = -1, .sclk_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(WS2812_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = WS2812_SPI_CLK, .mode = 0, .spics_io_num = -1, .queue_size = 4,
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
    const uint8_t cmds[] = {0xAE,0x20,0x00,0x40,0x81,0xCF,0xA1,0xA8,0x3F,0xC8,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,0xDB,0x40,0x8D,0x14,0xA4,0xA6,0xAF};
    for (int i = 0; i < sizeof(cmds); i++) oled_write_cmd(cmds[i]);
    oled_clear();
}

/* ============================ RainMaker ============================ */
esp_rmaker_device_t *home_device;

/* 写回调：APP控制LED和风扇 */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    if (strcmp(esp_rmaker_param_get_name(param), "LED") == 0) {
        ws2812_set_color(val.val.b ? 80 : 0, 0, 0);
        esp_rmaker_param_update(param, val);
    } else if (strcmp(esp_rmaker_param_get_name(param), "Relay") == 0) {
        gpio_set_level(FAN_GPIO, val.val.b ? 1 : 0);
        ESP_LOGI(TAG, "Relay %s (GPIO%d=%d)", val.val.b ? "ON" : "OFF", FAN_GPIO, val.val.b ? 1 : 0);
        esp_rmaker_param_update(param, val);
    } else if (strcmp(esp_rmaker_param_get_name(param), "Buzzer") == 0) {
        if (val.val.b) buzzer_beep();
        ESP_LOGI(TAG, "Buzzer %s", val.val.b ? "BEEP" : "OFF");
        esp_rmaker_param_update(param, val);
    } else if (strcmp(esp_rmaker_param_get_name(param), "Motor") == 0) {
        if (val.val.b) motor_step(512);  /* 约1圈（512步≈4096/8） */
        ESP_LOGI(TAG, "Motor %s", val.val.b ? "STEP" : "OFF");
        esp_rmaker_param_update(param, val);
    }
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:    ESP_LOGI(TAG, "RainMaker Init Done"); oled_show_text(3, "RM Init OK"); break;
            case RMAKER_EVENT_CLAIM_STARTED: ESP_LOGI(TAG, "Claim Started"); oled_show_text(3, "Claiming..."); break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL: ESP_LOGI(TAG, "Claim Success"); oled_show_text(3, "Claimed!"); break;
            case RMAKER_EVENT_CLAIM_FAILED: ESP_LOGE(TAG, "Claim Failed"); oled_show_text(3, "Claim Fail!"); break;
            default: ESP_LOGW(TAG, "Unhandled RainMaker Event: %" PRIi32, event_id); break;
        }
    } else if (event_base == APP_NETWORK_EVENT) {
        switch (event_id) {
            case APP_NETWORK_EVENT_QR_DISPLAY: {
                ESP_LOGI(TAG, "Provisioning QR : %s", (char *)event_data);
                /* 解析QR JSON，提取name和pop显示到OLED */
                char *qr = (char *)event_data;
                char name_str[32] = {0};
                char pop_str[32] = {0};
                char *p;
                p = strstr(qr, "\"name\":\"");
                if (p) { p += 8; char *e = strchr(p, '"'); if (e && e - p < 31) { memcpy(name_str, p, e - p); } }
                p = strstr(qr, "\"pop\":\"");
                if (p) { p += 7; char *e = strchr(p, '"'); if (e && e - p < 31) { memcpy(pop_str, p, e - p); } }
                oled_show_text(0, "Add Device:");
                oled_show_text(1, name_str);
                char pop_buf[40];
                snprintf(pop_buf, sizeof(pop_buf), "POP:%s", pop_str);
                oled_show_text(2, pop_buf);
                oled_show_text(3, "RainMaker APP");
                break;
            }
            case APP_NETWORK_EVENT_PROV_TIMEOUT: ESP_LOGI(TAG, "Provisioning Timeout"); break;
            case APP_NETWORK_EVENT_PROV_RESTART: ESP_LOGI(TAG, "Provisioning Restart"); break;
            default: break;
        }
    }
}

/* 传感器上报任务 */
static void sensor_task(void *arg)
{
    int temp = 25, humi = 60;
    char buf[20];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        /* 读取真实DHT11传感器 */
        int t, h;
        if (dht11_read(&h, &t) == 0) {
            temp = t;
            humi = h;
        }

        /* 读取烟雾传感器 */
        int smoke = smoke_read();

        if (home_device) {
            esp_rmaker_param_t *tp = esp_rmaker_device_get_param_by_name(home_device, "Temperature");
            esp_rmaker_param_t *hp = esp_rmaker_device_get_param_by_name(home_device, "Humidity");
            esp_rmaker_param_t *sp = esp_rmaker_device_get_param_by_name(home_device, "Smoke");
            if (tp) esp_rmaker_param_update_and_report(tp, esp_rmaker_float((float)temp));
            if (hp) esp_rmaker_param_update_and_report(hp, esp_rmaker_float((float)humi));
            if (sp) esp_rmaker_param_update_and_report(sp, esp_rmaker_int(smoke));
        }
        snprintf(buf, sizeof(buf), "T:%dC", temp); oled_show_text(1, buf);
        snprintf(buf, sizeof(buf), "H:%d%%", humi); oled_show_text(2, buf);
        ESP_LOGI(TAG, "DHT11: T=%d H=%d Smoke=%d", temp, humi, smoke);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Smart Home (ESP RainMaker) ===");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* LED + OLED + 外设初始化 */
    ws2812_init();
    ws2812_set_color(50, 0, 0);
    oled_init();
    oled_show_text(0, "Smart Home");
    gpio_config_t fan_conf = { .pin_bit_mask = (1ULL << FAN_GPIO), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&fan_conf);
    motor_init();
    buzzer_init();
    smoke_init();

    /* 初始化网络 */
    app_network_init();

    /* 注册事件 */
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    /* RainMaker节点 */
    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = false, };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP32 Smart Home", "Smart Home");
    if (!node) { ESP_LOGE(TAG, "Could not initialise node."); vTaskDelay(5000/portTICK_PERIOD_MS); abort(); }

    /* 创建设备 */
    home_device = esp_rmaker_device_create("Smart Home", NULL, NULL);
    esp_rmaker_device_add_cb(home_device, write_cb, NULL);
    esp_rmaker_device_add_param(home_device, esp_rmaker_name_param_create("Name", "Smart Home"));

    /* 温湿度 + 烟雾（只读） */
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Temperature", ESP_RMAKER_PARAM_TEMPERATURE, esp_rmaker_float(25.0), PROP_FLAG_READ));
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Humidity", ESP_RMAKER_PARAM_TEMPERATURE, esp_rmaker_float(60.0), PROP_FLAG_READ));
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Smoke", ESP_RMAKER_PARAM_TEMPERATURE, esp_rmaker_int(0), PROP_FLAG_READ));

    /* LED（读写） */
    esp_rmaker_param_t *led_param = esp_rmaker_param_create("LED", ESP_RMAKER_PARAM_POWER,
        esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(home_device, led_param);
    esp_rmaker_device_assign_primary_param(home_device, led_param);

    /* 继电器（读写） */
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Relay", ESP_RMAKER_PARAM_POWER,
        esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE));

    /* 蜂鸣器（读写） */
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Buzzer", ESP_RMAKER_PARAM_POWER,
        esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE));

    /* 步进电机（读写，ON=转一圈） */
    esp_rmaker_device_add_param(home_device,
        esp_rmaker_param_create("Motor", ESP_RMAKER_PARAM_POWER,
        esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE));

    esp_rmaker_node_add_device(node, home_device);

    /* 启动服务 */
    esp_rmaker_timezone_service_enable();
    esp_rmaker_schedule_enable();
    esp_rmaker_scenes_enable();
    app_insights_enable();
    esp_rmaker_start();

    /* 开始配网（POP_TYPE_RANDOM 从fctry分区读取，首次自动Claim） */
    app_network_set_custom_mfg_data(MFG_DATA_DEVICE_TYPE_SWITCH, MFG_DATA_DEVICE_SUBTYPE_SWITCH);
    app_network_start(POP_TYPE_RANDOM);

    /* 配网完成后启动传感器上报任务（避免覆盖OLED上的POP码显示）*/
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System Ready. Use ESP RainMaker APP to add device.");
}
