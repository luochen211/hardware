/*
 * 实验6：BLE GATT Server
 * 功能：手机通过nRF Connect连接ESP32，控制WS2812 RGB灯亮灭
 *
 * GATT服务表：
 *   Service:        0x00FF
 *   Characteristic: 0xFF01 (READ + WRITE)
 *
 * 实训箱引脚：WS2812 → GPIO14（SPI3驱动）
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
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_device.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"

static const char *TAG = "BLE_GATTS";

/* ============== OLED 引脚定义 ============== */
#define I2C_MASTER_SCL_IO  GPIO_NUM_19
#define I2C_MASTER_SDA_IO  GPIO_NUM_18
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define OLED_I2C_ADDR      0x3C

/* ============== WS2812 引脚定义 ============== */
#define WS2812_SPI_HOST    SPI3_HOST
#define WS2812_MOSI_PIN    14
#define WS2812_SPI_CLK     8000000

/* ============== BLE 参数定义 ============== */
#define DEVICE_NAME     "IoT_Student"
#define GATTS_APP_ID    0
#define GATTS_NUM_HANDLE 4

#define GATTS_SERVICE_UUID       0x00FF
#define GATTS_CHAR_UUID          0xFF01

enum {
    IDX_SVC,
    IDX_CHAR,
    IDX_CHAR_VAL,
    IDX_CHAR_CFG,
    IDX_NB,
};

static uint16_t s_gatts_handle_table[IDX_NB];
static uint16_t s_conn_id = 0;
static bool s_is_connected = false;

/* ============== OLED 驱动 ============== */
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

/* ============== WS2812 SPI 驱动（前向声明，供BLE回调使用）============== */
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
    spi_transaction_t t = { .length = 24 * 8, .tx_buffer = spi_buf };
    spi_device_polling_transmit(s_spi, &t);
    esp_rom_delay_us(60);
}

/* ============== 广播数据 ============== */
static uint8_t s_adv_raw_data[] = {
    0x02, 0x01, 0x06,
    0x0C, 0x09, 'I', 'o', 'T', '_', 'S', 't', 'u', 'd', 'e', 'n', 't',
    0x03, 0x03, 0xFF, 0x00,
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x0020,
    .adv_int_max = 0x0040,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ============== GATT 属性配置表 ============== */
static const esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&(uint16_t){0x2800},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint16_t),
            .length = sizeof(uint16_t),
            .value = (uint8_t *)&(uint16_t){GATTS_SERVICE_UUID},
        }
    },
    [IDX_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&(uint16_t){0x2803},
            .perm = ESP_GATT_PERM_READ,
            .max_length = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t),
            .length = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t),
            .value = (uint8_t *)&(uint8_t[]){
                ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,  /* properties: 可读可写 */
                0x00, 0x00,                                 /* value handle（运行时自动填充）*/
                GATTS_CHAR_UUID & 0xFF,                     /* UUID 低字节 */
                (GATTS_CHAR_UUID >> 8) & 0xFF,              /* UUID 高字节 */
            },
        }
    },
    [IDX_CHAR_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&(uint16_t){GATTS_CHAR_UUID},
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = 32,
            .length = 9,
            .value = (uint8_t *)"Hello BLE",
        }
    },
    [IDX_CHAR_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {
            .uuid_length = ESP_UUID_LEN_16,
            .uuid_p = (uint8_t *)&(uint16_t){0x2902},
            .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            .max_length = sizeof(uint16_t),
            .length = 0,           /* 初始长度=0，value必须非NULL或length=0 */
            .value = NULL,
        }
    },
};

/* ============== GATTS 事件回调 ============== */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT App registered, app_id=%d, status=%d",
                 param->reg.app_id, param->reg.status);
        if (param->reg.status == ESP_GATT_OK) {
            esp_ble_gap_set_device_name(DEVICE_NAME);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, 0);
        }
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "create attr table failed, error code=0x%x",
                     param->add_attr_tab.status);
            break;
        }
        if (param->add_attr_tab.num_handle == IDX_NB) {
            ESP_LOGI(TAG, "attr table created successfully");
            memcpy(s_gatts_handle_table, param->add_attr_tab.handles,
                   sizeof(s_gatts_handle_table));
            esp_ble_gatts_start_service(s_gatts_handle_table[IDX_SVC]);
        }
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "service started, status=%d", param->start.status);
        esp_ble_gap_config_adv_data_raw(s_adv_raw_data, sizeof(s_adv_raw_data));
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "client connected, conn_id=%d", param->connect.conn_id);
        s_conn_id = param->connect.conn_id;
        s_is_connected = true;
        esp_ble_gap_stop_advertising();
        oled_show_text(2, "Client Connected");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "client disconnected, reason=0x%x", param->disconnect.reason);
        s_is_connected = false;
        esp_ble_gap_start_advertising(&s_adv_params);
        oled_show_text(2, "Waiting...");
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(TAG, "read request from conn_id=%d", param->read.conn_id);
        /* 返回当前LED状态 */
        esp_gatt_rsp_t rsp = {0};
        rsp.attr_value.len = strlen("Hello BLE");
        memcpy(rsp.attr_value.value, "Hello BLE", rsp.attr_value.len);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                    param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "write request, len=%d", param->write.len);
        ESP_LOG_BUFFER_HEX(TAG, param->write.value, param->write.len);
        ESP_LOG_BUFFER_CHAR(TAG, param->write.value, param->write.len);

        if (param->write.len >= 2 &&
            strncmp((char *)param->write.value, "ON", 2) == 0) {
            ws2812_set_color(80, 0, 0);
            oled_show_text(3, "LED ON");
            ESP_LOGI(TAG, ">>> WS2812 ON");
        } else if (param->write.len >= 3 &&
                   strncmp((char *)param->write.value, "OFF", 3) == 0) {
            ws2812_set_color(0, 0, 0);
            oled_show_text(3, "LED OFF");
            ESP_LOGI(TAG, ">>> WS2812 OFF");
        } else {
            ESP_LOGW(TAG, "unknown command");
        }

        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                        param->write.trans_id,
                                        ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_DELETE_EVT:
        ESP_LOGI(TAG, "attr table deleted");
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU negotiated: %d", param->mtu.mtu);
        break;

    case ESP_GATTS_STOP_EVT:
        ESP_LOGI(TAG, "service stopped");
        break;

    case ESP_GATTS_RESPONSE_EVT:
        break;

    default:
        break;
    }
}

/* ============== GAP 事件回调 ============== */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "raw adv data set, status=%d",
                 param->adv_data_raw_cmpl.status);
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "adv start failed, status=%d",
                     param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "advertising started, waiting for connection...");
            ESP_LOGI(TAG, "Use nRF Connect to scan and connect to \"%s\"",
                     DEVICE_NAME);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "advertising stopped");
        break;

    default:
        break;
    }
}

/* ============== 主函数 ============== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== 实验6: BLE GATT Server ===");

    /* 1. 初始化 WS2812 + OLED */
    ws2812_init();
    oled_init();
    oled_show_text(0, "Exp6 BLE GATT");
    oled_show_text(1, "IoT_Student");
    oled_show_text(2, "Waiting...");
    oled_show_text(3, "LED OFF");

    /* 2. NVS Flash 初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 3. 释放经典蓝牙内存 */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* 4. 初始化 BT 控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 5. 启用 BT 控制器（BLE模式） */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 6. 初始化并启用 Bluedroid */
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 7. 注册回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gap reg failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gatts reg failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 8. 注册 GATT App */
    ESP_LOGI(TAG, "registering GATT App...");
    ret = esp_ble_gatts_app_register(GATTS_APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "app reg failed: %s", esp_err_to_name(ret));
        return;
    }

    /* 9. MTU设置（ESP-IDF v6已移除此API，使用默认MTU=23字节，够用） */

    ESP_LOGI(TAG, "BLE GATT Server initialized.");
    ESP_LOGI(TAG, "Device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Service UUID: 0x%04X", GATTS_SERVICE_UUID);
    ESP_LOGI(TAG, "Char UUID:    0x%04X", GATTS_CHAR_UUID);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试方法 ===");
    ESP_LOGI(TAG, "1. 手机安装 nRF Connect");
    ESP_LOGI(TAG, "2. 扫描找到 \"%s\"", DEVICE_NAME);
    ESP_LOGI(TAG, "3. 连接 → 找到 Service 0x00FF");
    ESP_LOGI(TAG, "4. 写入 \"ON\" → LED亮 | \"OFF\" → LED灭");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
