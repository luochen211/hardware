/**
 * ============================================================
 *  wifi_connect.h - WiFi STA 连接头文件
 * ============================================================
 */

#ifndef WIFI_CONNECT_H_
#define WIFI_CONNECT_H_

#include <stdint.h>

/**
 * @brief 初始化 WiFi STA 模式并连接到 AP
 *        阻塞直到连接成功或重试超过5次失败
 */
void wifi_init_sta(void);

/* IP获取成功回调（由main.c设置，用于OLED显示IP）*/
extern void (*g_on_ip_ready)(uint32_t ip1, uint32_t ip2, uint32_t ip3, uint32_t ip4);

#endif /* WIFI_CONNECT_H_ */
