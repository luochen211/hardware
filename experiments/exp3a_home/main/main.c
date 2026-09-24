/**
 * 实验3A：智能家居板传感器（DHT11温湿度 + 烟雾MQ-2 + OLED）
 * ====================================================================
 * 硬件：智能家居模块板（Smart_Home_Module）
 *
 * 引脚（实训箱源码HCS + 原理图验证）：
 *   DHT11温湿度:  GPIO5  （单总线）
 *   烟雾MQ-2:     GPIO34 （ADC1_CH6，输入专用引脚）
 *   OLED(I2C):    SCL=GPIO19, SDA=GPIO18, 地址0x3C
 *
 * 编译运行：
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "EXP3A";

#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"

/* --- DHT11 --- */
#define DHT11_PIN          GPIO_NUM_5   /* 实训箱硬件确认 */

/* --- 烟雾MQ-2 (GPIO34 = ADC1_CH6) --- */
#define SMOKE_ADC_CHANNEL  ADC_CHANNEL_7  /* GPIO35 */

/* --- I2C OLED --- */
#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

/* ============== DHT11 驱动（移植自实训箱 dht11_drive.c）============== */

static int dht11_read(int *humidity, int *temperature)
{
    uint8_t buf[5] = {0};

    /* GPIO5配置：开漏+上拉（单总线） */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT11_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* === 起始信号（dht11_rst）=== */
    gpio_set_level(DHT11_PIN, 0);    /* 拉低 */
    vTaskDelay(pdMS_TO_TICKS(20));   /* ≥18ms */
    gpio_set_level(DHT11_PIN, 1);    /* 释放（开漏=高阻态，上拉拉高） */

    portDISABLE_INTERRUPTS();
    esp_rom_delay_us(30);            /* 主机拉高20~40us */

    /* === 等待DHT11响应（dht11_check）=== */
    /* 等待DHT11拉低（80us）：while HIGH → LOW */
    int retry = 0;
    while (gpio_get_level(DHT11_PIN) && retry < 200) {
        esp_rom_delay_us(1);
        retry++;
    }
    if (retry >= 200) { portENABLE_INTERRUPTS(); return -1; }

    /* 等待DHT11拉高（80us）：while LOW → HIGH */
    retry = 0;
    while ((!gpio_get_level(DHT11_PIN)) && retry < 200) {
        esp_rom_delay_us(1);
        retry++;
    }
    if (retry >= 200) { portENABLE_INTERRUPTS(); return -1; }

    /* === 读取40位数据（dht11_read_byte × 5）=== */
    for (int byte_idx = 0; byte_idx < 5; byte_idx++) {
        uint8_t dat = 0;
        for (int bit = 0; bit < 8; bit++) {
            dat <<= 1;

            /* 等待变低（数据位前导低电平50us）*/
            retry = 0;
            while (gpio_get_level(DHT11_PIN) && retry < 200) {
                esp_rom_delay_us(1);
                retry++;
            }
            /* 等待变高（数据位高电平开始）*/
            retry = 0;
            while ((!gpio_get_level(DHT11_PIN)) && retry < 200) {
                esp_rom_delay_us(1);
                retry++;
            }
            /* 等待40us后采样：
             * "0"的高电平~26us → 40us后已变低 → 0
             * "1"的高电平~70us → 40us后仍高 → 1 */
            esp_rom_delay_us(40);
            if (gpio_get_level(DHT11_PIN))
                dat |= 1;
        }
        buf[byte_idx] = dat;
    }

    portENABLE_INTERRUPTS();

    /* === 校验和 === */
    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        return -2;
    }

    *humidity = buf[0];
    *temperature = buf[2];
    return 0;
}

/* ============== 烟雾MQ-2 ADC ============== */
static void smoke_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, SMOKE_ADC_CHANNEL, &config));
    ESP_LOGI(TAG, "烟雾ADC初始化: ADC1_CH7(GPIO35)");
}

static int smoke_read(void)
{
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        int raw;
        adc_oneshot_read(s_adc1_handle, SMOKE_ADC_CHANNEL, &raw);
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return sum / 5;
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

/* 5x8 ASCII字库（空格~）*/
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
    /* 先用空格填满整行（21字符 × 6列 = 126列），清除旧内容残留 */
    for (int i = 0; i < 21; i++) {
        for (int col = 0; col < 5; col++)
            oled_write_data(0x00);
        oled_write_data(0x00);
    }
    /* 回到行首，写入新内容 */
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

    /* SSD1306初始化序列 */
    const uint8_t cmds[] = {0xAE,0x20,0x00,0x40,0x81,0xCF,0xA1,0xA8,0x3F,
                            0xC8,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,
                            0xDB,0x40,0x8D,0x14,0xA4,0xA6,0xAF};
    for (int i = 0; i < sizeof(cmds); i++)
        oled_write_cmd(cmds[i]);
    oled_clear();
    ESP_LOGI(TAG, "OLED初始化: SCL=GPIO19 SDA=GPIO18");
}

/* ============== app_main ============== */
void app_main(void)
{
    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "实验3A: 智能家居板 DHT11+烟雾+OLED");
    ESP_LOGI(TAG, "学号: %s  姓名: %s", STUDENT_ID, STUDENT_NAME);
    ESP_LOGI(TAG, "============================");

    smoke_init();
    oled_init();

    oled_show_text(0, "IoT Smart Home");
    oled_show_text(1, "Starting...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    int loop = 0;
    while (1) {
        loop++;

        /* DHT11 */
        int humi = 0, temp = 0;
        int ret = dht11_read(&humi, &temp);

        /* 烟雾 */
        int smoke = smoke_read();

        ESP_LOGI(TAG, "--- 采集#%d ---", loop);
        if (ret == 0) {
            ESP_LOGI(TAG, "DHT11: T=%dC H=%d%%", temp, humi);
        } else {
            ESP_LOGI(TAG, "DHT11: 读取失败(%d)", ret);
        }
        ESP_LOGI(TAG, "烟雾: ADC=%d", smoke);

        /* OLED显示 */
        char buf[20];
        if (ret == 0)
            snprintf(buf, sizeof(buf), "T:%dC H:%d%%", temp, humi);
        else
            snprintf(buf, sizeof(buf), "DHT11:Err");
        oled_show_text(0, buf);

        snprintf(buf, sizeof(buf), "Smoke:%d", smoke);
        oled_show_text(1, buf);

        snprintf(buf, sizeof(buf), "Loop:%d", loop);
        oled_show_text(2, buf);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
