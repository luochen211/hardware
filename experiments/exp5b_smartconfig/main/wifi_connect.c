/**
 * ============================================================
 *  wifi_connect.c - SmartConfig（ESP-TOUCH）配网实现
 * ============================================================
 *
 *  配网流程：
 *    1. 先尝试从NVS读取已保存的WiFi信息
 *    2. 如果有 → 直接连接
 *    3. 如果没有 → 启动SmartConfig，等待手机APP配网
 *    4. 配网成功 → 连接WiFi并保存到NVS
 *    5. 下次上电自动连接（无需再次配网）
 *
 *  手机APP：ESP-TOUCH（Espressif官方，安卓/iOS免费）
 *  GitHub: https://github.com/EspressifApp/esp-touch
 *
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_smartconfig.h"

#include "wifi_connect.h"

static const char *TAG = "wifi";

/* IP获取成功回调（由main.c设置，用于OLED显示IP）*/
void (*g_on_ip_ready)(uint32_t ip1, uint32_t ip2, uint32_t ip3, uint32_t ip4) = NULL;

/* 事件标志位 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define ESPTOUCH_DONE_BIT   BIT2
static EventGroupHandle_t s_wifi_event_group;

/* NVS存储WiFi凭据的命名空间 */
#define NVS_NAMESPACE   "wifi_cred"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"
#define MAX_SSID_LEN    32
#define MAX_PASS_LEN    64

/* NVS：保存WiFi信息 */
static void save_wifi_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, NVS_KEY_SSID, ssid, strlen(ssid) + 1);
        nvs_set_blob(handle, NVS_KEY_PASS, password, strlen(password) + 1);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "WiFi凭据已保存到NVS");
    }
}

/* NVS：读取WiFi信息，返回true表示有保存 */
static bool load_wifi_from_nvs(char *ssid, char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return false;

    size_t ssid_len = MAX_SSID_LEN;
    size_t pass_len = MAX_PASS_LEN;
    esp_err_t r1 = nvs_get_blob(handle, NVS_KEY_SSID, ssid, &ssid_len);
    esp_err_t r2 = nvs_get_blob(handle, NVS_KEY_PASS, password, &pass_len);
    nvs_close(handle);

    if (r1 == ESP_OK && r2 == ESP_OK) {
        ESP_LOGI(TAG, "从NVS读取到WiFi信息: SSID=%s", ssid);
        return true;
    }
    return false;
}

/* WiFi/IP事件回调 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* STA启动事件由调用者处理（直接连接或SmartConfig） */
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi断开，尝试重连...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到IP: " IPSTR, IP2STR(&event->ip_info.ip));
        /* 外部回调：显示IP到OLED */
        if (g_on_ip_ready) {
            g_on_ip_ready(IP2STR(&event->ip_info.ip));
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* SmartConfig事件回调 */
static void sc_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_base == SC_EVENT) {
        switch (event_id) {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "[SmartConfig] 扫描完成");
            break;
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "[SmartConfig] 找到通道");
            break;
        case SC_EVENT_GOT_SSID_PSWD: {
            ESP_LOGI(TAG, "[SmartConfig] ✅ 收到WiFi信息!");
            smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

            /* 打印收到的SSID */
            char ssid[MAX_SSID_LEN] = {0};
            char password[MAX_PASS_LEN] = {0};
            strncpy(ssid, (char *)evt->ssid, sizeof(ssid) - 1);
            strncpy(password, (char *)evt->password, sizeof(password) - 1);
            ESP_LOGI(TAG, "[SmartConfig] SSID=%s", ssid);
            ESP_LOGI(TAG, "[SmartConfig] PASSWORD=%s", strlen(password) ? "***" : "(无密码)");

            /* 保存到NVS */
            save_wifi_to_nvs(ssid, password);

            /* 用收到的信息连接WiFi */
            wifi_config_t wifi_config = {0};
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set)
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
            break;
        }
        case SC_EVENT_SEND_ACK_DONE:
            ESP_LOGI(TAG, "[SmartConfig] 配网完成确认已发送");
            xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
        }
    }
}

/**
 * SmartConfig配网任务
 */
static void smartconfig_task(void *arg)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  请打开手机APP「ESP-TOUCH」进行配网");
    ESP_LOGI(TAG, "  1. 手机连接WiFi（2.4GHz）");
    ESP_LOGI(TAG, "  2. 打开ESP-TOUCH APP");
    ESP_LOGI(TAG, "  3. 输入WiFi密码");
    ESP_LOGI(TAG, "  4. 点击「开始配网」");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    /* 启动SmartConfig */
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    /* 等待配网完成 */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | ESPTOUCH_DONE_BIT,
        pdFALSE, pdTRUE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "SmartConfig配网成功！");
    }

    /* 停止SmartConfig */
    esp_smartconfig_stop();
    vTaskDelete(NULL);
}

/**
 * @brief 初始化WiFi（SmartConfig版）
 *        先尝试NVS已保存的信息，没有则启动SmartConfig
 */
void wifi_init_sta(void)
{
    /* 1. 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 2. 初始化协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 3. 创建事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. 创建STA接口 */
    esp_netif_create_default_wifi_sta();

    /* 5. WiFi初始化 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 6. 注册事件回调 */
    esp_event_handler_instance_t instance_wifi;
    esp_event_handler_instance_t instance_ip;
    esp_event_handler_instance_t instance_sc;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        SC_EVENT, ESP_EVENT_ANY_ID, &sc_event_handler, NULL, &instance_sc));

    /* 7. 设置STA模式并启动 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 8. 尝试从NVS读取已保存的WiFi信息 */
    char saved_ssid[MAX_SSID_LEN] = {0};
    char saved_pass[MAX_PASS_LEN] = {0};

    if (load_wifi_from_nvs(saved_ssid, saved_pass)) {
        /* --- 有保存的信息，直接连接 --- */
        ESP_LOGI(TAG, "使用已保存的WiFi连接: %s", saved_ssid);
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, saved_pass, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();

        /* 等待连接结果（最多10秒） */
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group, WIFI_CONNECTED_BIT,
            pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "✅ WiFi连接成功（已保存信息）");
            return;  /* 成功，直接返回 */
        }
        ESP_LOGW(TAG, "已保存的WiFi连接失败，启动SmartConfig...");
    }

    /* --- 没有保存信息或连接失败，启动SmartConfig --- */
    ESP_LOGI(TAG, "启动SmartConfig配网...");
    xTaskCreate(smartconfig_task, "smartconfig", 4096, NULL, 3, NULL);

    /* 等待连接成功（阻塞） */
    xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT,
        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "✅ WiFi连接成功（SmartConfig配网）");
}
