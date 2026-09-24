/**
 * 实验4A：智能家居板执行器综合控制
 * ====================================================================
 * 硬件：智能家居模块板（Smart_Home_Module）
 *
 * 4个执行器同时运行（多任务并行）：
 *   1. 蜂鸣器(GPIO13)：循环播放小星星旋律（LEDC PWM）
 *   2. 步进电机(IN1-4=25/33/32/35)：正转2圈→反转2圈（8拍驱动）
 *   3. WS2812(GPIO14)：RGB彩虹渐变（SPI驱动）
 *   4. 继电器(GPIO17)：拨动开关K1_C(GPIO22)控制开关
 *
 * 编译运行：
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "EXP4A";

#define STUDENT_ID   "2025xxxxxx"
#define STUDENT_NAME "张三"

/* --- 引脚定义 --- */
#define BUZZER_GPIO      13
#define STEPPER_IN1      26   // 源码motor_expand_drive_v2.c确认
#define STEPPER_IN2      25
#define STEPPER_IN3      33
#define STEPPER_IN4      32
#define RELAY_GPIO       17
#define WS2812_MOSI_PIN  14
#define BUTTON_K1C       22    /* 拨动开关K1_C（控制继电器）*/
#define BUTTON_K1D       21    /* 拨动开关K1_D（控制蜂鸣器）*/

/* --- LEDC 配置 --- */
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LEDC_FREQ        25000

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
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 8000000,
        .mode           = 0,
        .spics_io_num   = -1,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &s_spi));
    ESP_LOGI(TAG, "WS2812: SPI3 MOSI=GPIO%d", WS2812_MOSI_PIN);
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

/* ======================== 蜂鸣器 LEDC PWM ======================== */
#define NOTE_DO   262
#define NOTE_RE   294
#define NOTE_MI   330
#define NOTE_FA   349
#define NOTE_SOL  392
#define NOTE_LA   440
#define NOTE_SI   494
#define NOTE_REST 0

static const int melody[] = {
    NOTE_DO, NOTE_DO, NOTE_SOL, NOTE_SOL,
    NOTE_LA, NOTE_LA, NOTE_SOL, NOTE_REST,
    NOTE_FA, NOTE_FA, NOTE_MI, NOTE_MI,
    NOTE_RE, NOTE_RE, NOTE_DO, NOTE_REST,
};
#define MELODY_LEN   (sizeof(melody) / sizeof(melody[0]))
#define NOTE_DUR_MS  400

static void buzzer_play_note(int freq, int duration_ms)
{
    if (freq == NOTE_REST) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (1 << LEDC_DUTY_RES) / 2);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan_cfg = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = BUZZER_GPIO,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));
    ESP_LOGI(TAG, "蜂鸣器: GPIO%d LEDC PWM", BUZZER_GPIO);
}

/* ======================== 步进电机 ======================== */
/* 8拍驱动序列 */
static const uint8_t step_seq[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1},
};
#define STEPPER_STEPS_PER_REV  4096  /* 28BYJ48减速比1:64，一圈≈4096步 */

static void stepper_set_pins(uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3)
{
    gpio_set_level(STEPPER_IN1, s0);
    gpio_set_level(STEPPER_IN2, s1);
    gpio_set_level(STEPPER_IN3, s2);
    gpio_set_level(STEPPER_IN4, s3);
}

static void stepper_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<STEPPER_IN1)|(1ULL<<STEPPER_IN2)|
                        (1ULL<<STEPPER_IN3)|(1ULL<<STEPPER_IN4),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "步进电机: IN1-4=GPIO%d/%d/%d/%d",
             STEPPER_IN1, STEPPER_IN2, STEPPER_IN3, STEPPER_IN4);
}

static void stepper_rotate(int steps, int delay_ms)
{
    int dir = (steps >= 0) ? 1 : -1;
    int count = (steps >= 0) ? steps : -steps;
    for (int s = 0; s < count; s++) {
        int phase = (s * dir) % 8;
        if (phase < 0) phase += 8;
        stepper_set_pins(step_seq[phase][0], step_seq[phase][1],
                         step_seq[phase][2], step_seq[phase][3]);
        /* 步进电机需要精确毫秒延时，vTaskDelay精度不够（tick=10ms）
         * 用esp_rom_delay_us做精确延时，同时用vTaskDelay让出CPU */
        esp_rom_delay_us(delay_ms * 1000);
        if (s % 20 == 0) vTaskDelay(1);  /* 每20步让出一次CPU，防止看门狗 */
    }
    /* 停止：断电 */
    stepper_set_pins(0, 0, 0, 0);
}

/* ======================== 继电器+拨动开关 ======================== */
static void relay_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(RELAY_GPIO, 0);

    gpio_config_t btn = {
        .pin_bit_mask = (1ULL<<BUTTON_K1C) | (1ULL<<BUTTON_K1D),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);
    ESP_LOGI(TAG, "继电器: GPIO%d, 蜂鸣器开关: K1_D(GPIO%d)", RELAY_GPIO, BUTTON_K1D);
}

/* ======================== FreeRTOS 任务 ======================== */

/* 任务1：蜂鸣器循环播放小星星（K1_D拨动开关控制开关） */
static void buzzer_task(void *arg)
{
    while (1) {
        /* 检查K1_D拨动开关：拨到D档(GPIO21接地=0)时播放，拨离时静音 */
        if (gpio_get_level(BUTTON_K1D) == 0) {
            for (int i = 0; i < MELODY_LEN; i++)
                buzzer_play_note(melody[i], NOTE_DUR_MS);
            ESP_LOGI(TAG, "[蜂鸣器] 小星星播放完毕");
        } else {
            /* 静音状态：确保蜂鸣器关闭 */
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* 任务2：步进电机正反转 */
static void stepper_task(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "[步进电机] 正转1圈");
        stepper_rotate(STEPPER_STEPS_PER_REV, 3);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "[步进电机] 反转1圈");
        stepper_rotate(-STEPPER_STEPS_PER_REV, 3);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* 任务3：WS2812彩虹渐变 */
static void ws2812_task(void *arg)
{
    uint8_t r = 255, g = 0, b = 0;
    int phase = 0;
    while (1) {
        for (int i = 0; i < 255; i += 15) {
            if      (phase == 0) { r -= 15; g += 15; }
            else if (phase == 1) { g -= 15; b += 15; }
            else                 { b -= 15; r += 15; }
            ws2812_set_color(r, g, b);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        phase = (phase + 1) % 3;
    }
}

/* 任务4：拨动开关控制继电器 */
static void relay_task(void *arg)
{
    int last_state = 1;
    while (1) {
        int cur = gpio_get_level(BUTTON_K1C);
        if (cur != last_state) {
            vTaskDelay(pdMS_TO_TICKS(20));  /* 消抖 */
            cur = gpio_get_level(BUTTON_K1C);
            if (cur == 0) {
                gpio_set_level(RELAY_GPIO, 1);
                ESP_LOGI(TAG, "[继电器] ON (拨动开关拨到C档)");
            } else {
                gpio_set_level(RELAY_GPIO, 0);
                ESP_LOGI(TAG, "[继电器] OFF (拨动开关拨离C档)");
            }
            last_state = cur;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ======================== app_main ======================== */
void app_main(void)
{
    ESP_LOGI(TAG, "============================");
    ESP_LOGI(TAG, "实验4A: 智能家居板执行器综合控制");
    ESP_LOGI(TAG, "学号: %s  姓名: %s", STUDENT_ID, STUDENT_NAME);
    ESP_LOGI(TAG, "============================");

    /* 初始化所有外设 */
    buzzer_init();
    stepper_init();
    relay_button_init();
    ws2812_init();

    /* 创建4个并行任务 */
    xTaskCreate(buzzer_task,   "buzzer",   3072, NULL, 2, NULL);
    xTaskCreate(stepper_task,  "stepper",  3072, NULL, 3, NULL);
    xTaskCreate(ws2812_task,   "ws2812",   3072, NULL, 1, NULL);
    xTaskCreate(relay_task,    "relay",    2048, NULL, 4, NULL);

    ESP_LOGI(TAG, "4个任务已启动：蜂鸣器+步进电机+WS2812+继电器");
}
