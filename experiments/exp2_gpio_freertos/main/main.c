/**
 * 实验2：FreeRTOS多任务 + GPIO + 按键检测
 * ========================================
 * 创建3个FreeRTOS任务并行运行：
 *   任务1(blink_task)：WS2812 LED闪烁，1秒周期
 *   任务2(button_task)：按键检测(K1_C=IO22)，消抖+计数
 *   任务3(print_task)：每5秒串口打印按键计数
 *
 * 进阶功能：
 *   - 互斥量(Mutex)保护按键计数共享变量
 *   - 长按(≥500ms)切换呼吸灯模式，短按切换LED亮灭
 *
 * 硬件引脚（实训箱）：
 *   LED:  WS2812 → GPIO14（SPI3驱动）
 *   按键: K1_C = IO22（拨动开关）（上拉输入，按下接地）
 *   按键2: K1_D = IO21（拨动开关）（上拉输入，按下接地）
 *
 * 编译运行：
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"

/* ======================== 宏定义 ======================== */
static const char *TAG = "EXP2_GPIO";

#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"

/* --- WS2812 引脚（实训箱SPI灯条） --- */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000

/* --- 按键引脚 --- */
/* --- 按键引脚（实训箱拨动开关 WS-001DB）---
 * K1_B = GPIO23, K1_C = GPIO22, K1_D = GPIO21
 * 注意：这些是三档拨动开关，拨到对应档位时该引脚接地(低电平)
 * 实验用 K1_C(GPIO22) 作为主按键
 */
#define BUTTON_K1C_GPIO    GPIO_NUM_22       /* K1_C 拨动开关 */
#define BUTTON_K1D_GPIO    GPIO_NUM_21       /* K1_D 拨动开关 */

/* --- 时序参数 --- */
#define DEBOUNCE_MS        20
#define LONG_PRESS_MS      500
#define BLINK_PERIOD_MS    1000

/* ======================== WS2812 SPI 驱动 ======================== */
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
    spi_transaction_t t = {
        .length    = 24 * 8,
        .tx_buffer = spi_buf,
    };
    spi_device_polling_transmit(s_spi, &t);
    esp_rom_delay_us(60);
}

/* ======================== 共享变量与互斥量 ======================== */
static volatile int g_button_count = 0;        /* 按键按下次数 */
static volatile int g_led_mode = 0;            /* 0=闪烁, 1=呼吸灯 */
static SemaphoreHandle_t g_count_mutex = NULL; /* 保护g_button_count */

/* ======================== 任务1：LED闪烁 ======================== */
static void blink_task(void *arg)
{
    ESP_LOGI(TAG, "blink_task 启动");
    int led_state = 0;

    while (1) {
        led_state = !led_state;

        if (g_led_mode == 0) {
            /* 模式0：普通闪烁（红色） */
            ws2812_set_color(led_state ? 30 : 0, 0, 0);
        } else {
            /* 模式1：呼吸灯（绿色渐变） */
            for (int duty = 0; duty <= 80; duty += 5) {
                ws2812_set_color(0, duty, 0);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            for (int duty = 80; duty >= 0; duty -= 5) {
                ws2812_set_color(0, duty, 0);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS / 2));
    }
}

/* ======================== 任务2：按键检测 ======================== */
static void button_task(void *arg)
{
    ESP_LOGI(TAG, "button_task 启动");

    int last_state = 1;  /* 上拉，默认高电平 */
    int press_time_ms = 0;
    int is_pressing = 0;

    while (1) {
        int cur_state = gpio_get_level(BUTTON_K1C_GPIO);

        if (cur_state == 0 && last_state == 1) {
            /* 检测到按下（下降沿） */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));  /* 消抖 */
            if (gpio_get_level(BUTTON_K1C_GPIO) == 0) {
                press_time_ms = 0;
                is_pressing = 1;
            }
        }

        if (is_pressing && cur_state == 0) {
            press_time_ms += 10;
            if (press_time_ms >= LONG_PRESS_MS && g_led_mode == 0) {
                /* 长按：切换到呼吸灯模式 */
                g_led_mode = 1;
                ESP_LOGI(TAG, ">>> 长按 → 切换呼吸灯模式");
                xSemaphoreTake(g_count_mutex, portMAX_DELAY);
                g_button_count++;
                xSemaphoreGive(g_count_mutex);
                is_pressing = 0;
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }

        if (cur_state == 1 && last_state == 0 && is_pressing) {
            /* 释放 */
            if (press_time_ms < LONG_PRESS_MS) {
                /* 短按：切换LED亮灭 */
                g_led_mode = 0;
                ESP_LOGI(TAG, ">>> 短按 → 闪烁模式");
                xSemaphoreTake(g_count_mutex, portMAX_DELAY);
                g_button_count++;
                xSemaphoreGive(g_count_mutex);
            }
            is_pressing = 0;
        }

        last_state = cur_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ======================== 任务3：打印计数 ======================== */
static void print_task(void *arg)
{
    ESP_LOGI(TAG, "print_task 启动");
    int last_count = 0;

    while (1) {
        xSemaphoreTake(g_count_mutex, portMAX_DELAY);
        int count = g_button_count;
        xSemaphoreGive(g_count_mutex);

        if (count != last_count) {
            ESP_LOGI(TAG, "按键计数: %d (学号:%s)", count, STUDENT_ID);
            last_count = count;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ======================== app_main ======================== */
void app_main(void)
{
    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "实验2：FreeRTOS多任务 + GPIO");
    ESP_LOGI(TAG, "学号: %s  姓名: %s", STUDENT_ID, STUDENT_NAME);
    ESP_LOGI(TAG, "============================");

    /* 1. 初始化WS2812 */
    ws2812_init();

    /* 2. 配置按键GPIO */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_K1C_GPIO) | (1ULL << BUTTON_K1D_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    /* 3. 创建互斥量 */
    g_count_mutex = xSemaphoreCreateMutex();

    /* 4. 创建3个任务 */
    xTaskCreate(blink_task,   "blink_task",   4096, NULL, 1, NULL);
    xTaskCreate(button_task,  "button_task",  4096, NULL, 2, NULL);
    xTaskCreate(print_task,   "print_task",   4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "3个任务已创建，开始运行...");
}
