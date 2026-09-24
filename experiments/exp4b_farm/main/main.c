/**
 * 实验4B：智慧农业板执行器控制（风扇 + WS2812 RGB灯）
 * ====================================================================
 * 硬件：智慧农业模块板（Smart_Agriculture_Module）
 *
 * 引脚（用户确认）：
 *   风扇:         GPIO21（PWM调速）
 *   WS2812 RGB灯: GPIO14（SPI3驱动）
 *   拨动开关:     K1_C=GPIO22, K1_D=GPIO21...
 *   注意：风扇GPIO21与K1_D拨动开关冲突，风扇运行时不使用拨动开关
 *
 * 功能：
 *   1. 基础：风扇PWM调速（0→100%→0渐变）
 *   2. 进阶：WS2812 RGB颜色渐变
 *   3. 扩展：风扇转速与RGB颜色联动
 *
 * 编译运行：
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "EXP4B";

#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"

/* --- 风扇（开关控制，非PWM调速，原理图确认）--- */
#define FAN_GPIO         21

/* --- WS2812 SPI驱动 --- */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000

static spi_device_handle_t s_spi;

/* ============== WS2812 SPI 驱动 ============== */
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
    ESP_LOGI(TAG, "WS2812初始化: SPI%d MOSI=GPIO%d", WS2812_SPI_HOST, WS2812_MOSI_PIN);
}

static void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = { g, r, b };
    uint8_t spi_buf[24];
    int idx = 0;
    for (int c = 0; c < 3; c++) {
        for (int bit = 7; bit >= 0; bit--) {
            spi_buf[idx++] = (grb[c] >> bit) & 1 ? 0xFC : 0xE0;
        }
    }
    spi_transaction_t t = { .length = 24 * 8, .tx_buffer = spi_buf };
    spi_device_polling_transmit(s_spi, &t);
    esp_rom_delay_us(60);
}

/* ============== 风扇 LEDC PWM 驱动 ============== */
static void fan_init(void)
{
    /* 实训箱风扇通过MOS管做开关控制（非PWM调速），用普通GPIO输出即可 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FAN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(FAN_GPIO, 0);  /* 默认关闭 */
    ESP_LOGI(TAG, "风扇: GPIO%d (开关控制，不支持调速)", FAN_GPIO);
}

/**
 * 设置风扇转速
 * @param percent 0=停止，1-100=全速运行（实训箱风扇只有开关控制，不支持调速）
 */
static void fan_set_speed(int percent)
{
    /* 实训箱风扇通过MOS管(AO3401A+AO3402)做开关控制，无PWM调速功能
     * percent>0 = 全速运行，percent=0 = 停止 */
    if (percent > 0) {
        gpio_set_level(FAN_GPIO, 1);  /* 开启风扇（全速）*/
    } else {
        gpio_set_level(FAN_GPIO, 0);  /* 关闭风扇 */
    }
}

/* --- 拨动开关 K1_C (GPIO22) 控制风扇 --- */
#define BUTTON_K1C       22

/* ============== app_main ============== */
void app_main(void)
{
    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "实验4B: 智慧农业板 风扇+WS2812");
    ESP_LOGI(TAG, "学号: %s  姓名: %s", STUDENT_ID, STUDENT_NAME);
    ESP_LOGI(TAG, "============================");

    fan_init();
    ws2812_init();

    /* 配置K1_C拨动开关为输入（上拉） */
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << BUTTON_K1C),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);
    ESP_LOGI(TAG, "拨动开关 K1_C=GPIO%d（控制风扇开关）", BUTTON_K1C);

    int fan_on = 0;
    int last_btn = 1;  /* 上拉，默认高电平 */

    ESP_LOGI(TAG, "点按K1_C：开风扇+绿灯；再按一次：关风扇+彩虹");

    while (1) {
        int cur_btn = gpio_get_level(BUTTON_K1C);

        /* 检测下降沿（按下瞬间：高→低），消抖20ms */
        if (last_btn == 1 && cur_btn == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (gpio_get_level(BUTTON_K1C) == 0) {
                fan_on = !fan_on;  /* 翻转状态 */
                if (fan_on) {
                    fan_set_speed(100);
                    ws2812_set_color(0, 80, 0);
                    ESP_LOGI(TAG, "[风扇] ON + 绿灯");
                } else {
                    fan_set_speed(0);
                    ws2812_set_color(0, 0, 0);
                    ESP_LOGI(TAG, "[风扇] OFF");
                }
                /* 等待松开，防止连按 */
                while (gpio_get_level(BUTTON_K1C) == 0)
                    vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        last_btn = cur_btn;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
