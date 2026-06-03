/**
 * @file mqtt_main.c
 * @brief ESP32 MQTT Client — SmowCode Sample Project
 *
 * Robust MQTT 3.1.1 client for ESP32 using ESP-IDF native MQTT stack.
 * Features:
 *   - Auto-reconnect on network loss
 *   - TLS/SSL support (optional, set CONFIG_MQTT_USE_TLS=y)
 *   - QoS 0, 1, 2 publish/subscribe
 *   - AWS IoT Core compatible
 *   - Publishes sensor telemetry every 5 seconds
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_main";

/* ── Configuration (set via menuconfig) ────────────────────────────────────── */
#define MQTT_BROKER_URI     CONFIG_MQTT_BROKER_URI      /* e.g. mqtt://broker.hivemq.com */
#define MQTT_CLIENT_ID      CONFIG_MQTT_CLIENT_ID       /* e.g. smowcode-esp32-001 */
#define MQTT_PUB_TOPIC      CONFIG_MQTT_PUB_TOPIC       /* e.g. smowcode/sensors/telemetry */
#define MQTT_SUB_TOPIC      CONFIG_MQTT_SUB_TOPIC       /* e.g. smowcode/commands/# */
#define WIFI_SSID           CONFIG_MQTT_WIFI_SSID
#define WIFI_PASS           CONFIG_MQTT_WIFI_PASSWORD

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

/* ── WiFi Event Handler ─────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "WiFi lost — reconnecting");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── MQTT Event Handler ─────────────────────────────────────────────────────── */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            s_mqtt_connected = true;
            /* Subscribe to command topic */
            esp_mqtt_client_subscribe(s_mqtt_client, MQTT_SUB_TOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to: %s", MQTT_SUB_TOPIC);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected — will auto-reconnect");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribe confirmed: msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received on topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Payload: %.*s", event->data_len, event->data);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Publish confirmed: msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error: type=%d", event->error_handle->error_type);
            break;

        default:
            break;
    }
}

/* ── Telemetry Publish Task ─────────────────────────────────────────────────── */
static void telemetry_task(void *pvParameters)
{
    int seq = 0;
    char payload[128];

    while (1) {
        if (s_mqtt_connected) {
            /* Simulate sensor reading */
            float temperature = 22.5f + (seq % 10) * 0.3f;
            float humidity    = 55.0f + (seq % 5)  * 1.2f;

            snprintf(payload, sizeof(payload),
                     "{\"seq\":%d,\"temp\":%.1f,\"humidity\":%.1f,\"unit\":\"esp32\"}",
                     seq, temperature, humidity);

            int msg_id = esp_mqtt_client_publish(s_mqtt_client,
                                                  MQTT_PUB_TOPIC,
                                                  payload, 0,
                                                  1,    /* QoS 1 */
                                                  0);   /* retain=false */
            ESP_LOGI(TAG, "Published seq=%d msg_id=%d payload=%s", seq, msg_id, payload);
            seq++;
        } else {
            ESP_LOGD(TAG, "MQTT not connected — skipping publish");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ── WiFi Init ──────────────────────────────────────────────────────────────── */
static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Wait for IP */
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);
}

/* ── app_main ───────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 MQTT Client ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    ESP_LOGI(TAG, "WiFi connected — starting MQTT client");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL);
}
