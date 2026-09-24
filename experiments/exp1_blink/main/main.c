/**
 * 实验1：WS2812 RGB灯颜色循环（SPI驱动）
 * ============================================================
 * 当前工程使用 SPI3_HOST，MOSI=GPIO14；接线应以实际板卡原理图为准。
 * SPI字节编码参考实训箱OpenHarmony源码 ws2812_drive.c：
 *   - SPI3_HOST，MOSI=GPIO14
 *   - SPI时钟 8MHz（每bit 125ns，8bit=1us，接近WS2812位周期1.25us）
 *   - 0xFC(11111100) → WS2812的"1"码（高6bit+低2bit）
 *   - 0xE0(11100000) → WS2812的"0"码（高3bit+低5bit）
 *   - 数据顺序：GRB
 *
 * 编译运行：
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/spi_master.h"

static const char *TAG = "EXP1";

#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"

/* === WS2812 硬件配置（实训箱原理图+HCS配置）=== */
#define WS2812_SPI_HOST    SPI3_HOST       /* VSPI_HOST = SPI3_HOST */
#define WS2812_MOSI_PIN    14              /* WS2812数据线(GPIO14) */
#define WS2812_SPI_CLK     8000000         /* 8MHz（实训箱源码同款频率） */

static spi_device_handle_t s_spi;

/**
 * 初始化WS2812（通过SPI）
 * 复刻 ws2812_drive.c 的 ws2812_init()
 */
static void ws2812_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = WS2812_MOSI_PIN,
        .miso_io_num     = -1,
        .sclk_io_num     = -1,    /* WS2812不需要时钟线 */
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

    ESP_LOGI(TAG, "WS2812初始化: SPI%d MOSI=GPIO%d %.1fMHz",
             WS2812_SPI_HOST, WS2812_MOSI_PIN, WS2812_SPI_CLK/1000000.0);
}

/**
 * 发送RGB颜色（GRB顺序）
 * 复刻 ws2812_drive.c 的 ws2812_spi_write()
 */
static void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = { g, r, b };
    uint8_t spi_buf[24];
    int idx = 0;

    /* 每个颜色bit映射为1个SPI字节 */
    for (int c = 0; c < 3; c++) {
        for (int bit = 7; bit >= 0; bit--) {
            spi_buf[idx++] = (grb[c] >> bit) & 1 ? 0xFC : 0xE0;
        }
    }

    spi_transaction_t t = {
        .length    = 24 * 8,
        .tx_buffer = spi_buf,
    };
    spi_device_polling_transmit(s_spi, &t);

    /* WS2812复位：>50us低电平 */
    esp_rom_delay_us(60);
}

void app_main(void)
{
    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "实验1：WS2812 RGB灯（SPI驱动）");
    ESP_LOGI(TAG, "学号: %s  姓名: %s", STUDENT_ID, STUDENT_NAME);
    ESP_LOGI(TAG, "============================");

    ws2812_init();

    /* 先测试4种基本颜色，确认灯能亮 */
    ESP_LOGI(TAG, ">>> 颜色测试：红→绿→蓝→白 <<<");

    while (1) {
        ESP_LOGI(TAG, "红色");
        for (int i = 0; i < 20; i++) ws2812_set_color(80, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "绿色");
        for (int i = 0; i < 20; i++) ws2812_set_color(0, 80, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "蓝色");
        for (int i = 0; i < 20; i++) ws2812_set_color(0, 0, 80);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "白色");
        for (int i = 0; i < 20; i++) ws2812_set_color(40, 40, 40);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "熄灭");
        for (int i = 0; i < 20; i++) ws2812_set_color(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
