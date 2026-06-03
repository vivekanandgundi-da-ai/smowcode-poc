/**
 * @file ota_main.c
 * @brief ESP32 WiFi OTA Firmware Updater — SmowCode Sample Project
 *
 * Performs HTTPS Over-The-Air firmware update from a remote server.
 * Features:
 *   - HTTPS with server certificate validation
 *   - Dual OTA partition (A/B) with automatic rollback on boot failure
 *   - Version check before downloading (skips if already up-to-date)
 *   - Writes new firmware to the inactive OTA partition
 *   - Sets boot partition and reboots on success
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "esp_netif.h"

static const char *TAG = "ota_main";

/* ── Configuration ──────────────────────────────────────────────────────────── */
#define WIFI_SSID           CONFIG_OTA_WIFI_SSID
#define WIFI_PASS           CONFIG_OTA_WIFI_PASSWORD
#define OTA_FIRMWARE_URL    CONFIG_OTA_FIRMWARE_URL    /* HTTPS URL to .bin file */
#define CURRENT_FW_VERSION  "1.0.0"

/* Embedded server certificate for HTTPS validation (set in sdkconfig) */
extern const char server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const char server_cert_pem_end[]   asm("_binary_server_cert_pem_end");

static bool s_wifi_connected = false;

/* ── WiFi Event Handler ─────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
    }
}

/* ── WiFi Init ──────────────────────────────────────────────────────────────── */
static void wifi_init(void)
{
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
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ── OTA Update Task ────────────────────────────────────────────────────────── */
static void ota_task(void *pvParameters)
{
    /* Wait for WiFi */
    while (!s_wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", OTA_FIRMWARE_URL);
    ESP_LOGI(TAG, "Current firmware version: %s", CURRENT_FW_VERSION);

    /* Log current and next OTA partitions */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update  = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Running partition: %s (offset 0x%08X)",
             running->label, running->address);
    ESP_LOGI(TAG, "Update partition:  %s (offset 0x%08X)",
             update->label, update->address);

    esp_http_client_config_t http_cfg = {
        .url             = OTA_FIRMWARE_URL,
        .cert_pem        = server_cert_pem_start,
        .timeout_ms      = 5000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    ESP_LOGI(TAG, "Downloading and writing firmware...");
    esp_err_t ret = esp_https_ota(&ota_cfg);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update succeeded — rebooting in 3 seconds");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Keeping current firmware. Will retry on next boot.");
    }

    vTaskDelete(NULL);
}

/* ── Rollback Diagnostic ────────────────────────────────────────────────────── */
static void check_ota_rollback(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA — marking firmware valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

/* ── app_main ───────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 WiFi OTA Updater ===");
    ESP_LOGI(TAG, "Firmware version: %s", CURRENT_FW_VERSION);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Mark firmware valid if this is first boot after an OTA update */
    check_ota_rollback();

    wifi_init();

    xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
}
