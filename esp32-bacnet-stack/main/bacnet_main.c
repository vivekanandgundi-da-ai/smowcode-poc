/**
 * @file bacnet_main.c
 * @brief ESP32 BACnet/IP Stack — SmowCode Sample Project
 *
 * Implements a BACnet/IP device on ESP32 using ESP-IDF.
 * Supports:
 *   - BACnet/IP (Annex J) over UDP port 47808
 *   - ReadProperty / WriteProperty services
 *   - Who-Is / I-Am device discovery
 *   - Change of Value (COV) subscriptions
 *
 * Hardware: ESP32 with WiFi connection to BACnet/IP network
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

#include "bacnet_device.h"
#include "bacnet_handlers.h"

static const char *TAG = "bacnet_main";

/* BACnet/IP port (default 47808 = 0xBAC0) */
#define BACNET_IP_PORT      47808
#define BACNET_DEVICE_ID    1234
#define BACNET_DEVICE_NAME  "SmowCode-ESP32-BACnet"

/* WiFi credentials — set via menuconfig or sdkconfig */
#define WIFI_SSID   CONFIG_BACNET_WIFI_SSID
#define WIFI_PASS   CONFIG_BACNET_WIFI_PASSWORD

/* ─── WiFi Event Handler ──────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected — retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        bacnet_device_set_ip(event->ip_info.ip.addr);
    }
}

/* ─── WiFi Initialisation ────────────────────────────────────────────────── */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL,
                                                         &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialised. Connecting to SSID: %s", WIFI_SSID);
}

/* ─── BACnet Task ────────────────────────────────────────────────────────── */
static void bacnet_task(void *pvParameters)
{
    ESP_LOGI(TAG, "BACnet task started. Device ID: %d", BACNET_DEVICE_ID);

    bacnet_device_init(BACNET_DEVICE_ID, BACNET_DEVICE_NAME);
    bacnet_handlers_init();

    /* Open UDP socket for BACnet/IP */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create BACnet socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(BACNET_IP_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "BACnet/IP listening on port %d", BACNET_IP_PORT);

    uint8_t rx_buf[1500];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    while (1) {
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                           (struct sockaddr *)&sender, &sender_len);
        if (len > 0) {
            ESP_LOGD(TAG, "Received %d bytes from " IPSTR, len, IP2STR(&sender.sin_addr));
            bacnet_handlers_process(sock, rx_buf, len, &sender);
        } else {
            ESP_LOGW(TAG, "recvfrom error: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ─── app_main ───────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 BACnet Stack ===");
    ESP_LOGI(TAG, "BACnet Device ID   : %d", BACNET_DEVICE_ID);
    ESP_LOGI(TAG, "BACnet Device Name : %s", BACNET_DEVICE_NAME);
    ESP_LOGI(TAG, "BACnet/IP Port     : %d", BACNET_IP_PORT);

    /* Initialise NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Connect to WiFi */
    wifi_init();

    /* Wait for IP before starting BACnet */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Start BACnet task (stack: 8KB, priority: 5) */
    xTaskCreate(bacnet_task, "bacnet_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialisation complete.");
}
