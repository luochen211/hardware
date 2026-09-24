/**
 * ============================================================
 *  wifi_connect.c - WiFi STA 连接实现
 * ============================================================
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_connect.h"

/* ====== WiFi 配置：请修改为你的 WiFi 名称和密码 ====== */
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"
/* ==================================================== */

static const char *TAG = "wifi";

/* IP获取成功回调（由main.c设置，用于OLED显示IP）*/
void (*g_on_ip_ready)(uint32_t ip1, uint32_t ip2, uint32_t ip3, uint32_t ip4) = NULL;

/* 事件组标志位 */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0    /* 连接成功 */
#define WIFI_FAIL_BIT       BIT1    /* 连接失败 */
#define WIFI_MAXIMUM_RETRY  5       /* 最大重试次数 */
static int s_retry_num = 0;

/**
 * @brief WiFi/IP 事件回调处理函数
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* STA 启动 → 开始连接 */
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* 断开 → 重试 */
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "重试连接 AP... (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            /* 超过最大重试次数 → 设置失败标志 */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "连接失败，已达到最大重试次数");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* 获取到 IP 地址 → 连接成功 */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到 IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        /* 外部回调：显示IP到OLED */
        if (g_on_ip_ready) {
            g_on_ip_ready(IP2STR(&event->ip_info.ip));
        }
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief 初始化 WiFi STA 模式
 */
void wifi_init_sta(void)
{
    /* 1. 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 2. 初始化 TCP/IP 协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 3. 创建默认事件循环（如果尚未创建） */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. 创建默认 STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 5. WiFi 初始化（使用默认配置） */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 6. 注册事件回调 */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    /* 7. 配置 WiFi STA */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* 8. 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA 初始化完成，正在连接 %s ...", WIFI_SSID);

    /* 9. 等待连接结果（阻塞） */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ WiFi 连接成功: SSID=%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "❌ WiFi 连接失败: SSID=%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "❌ WiFi 连接: 未知事件");
    }

    /* 事件组可以保留用于后续断线检测 */
    // xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
}
