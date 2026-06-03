/**
 * @file ble_main.c
 * @brief ESP32 BLE Sensor Hub — SmowCode Sample Project
 *
 * Acts as a BLE GATT server exposing sensor data via custom services.
 * A BLE central (phone/gateway) can connect and read/subscribe to:
 *   - Temperature characteristic (notify)
 *   - Humidity characteristic (notify)
 *   - Accelerometer characteristic (notify)
 *
 * Uses the standard Environmental Sensing Service (UUID 0x181A) profile.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "nvs_flash.h"

static const char *TAG = "ble_sensor";

/* ── BLE Device Name ────────────────────────────────────────────────────────── */
#define DEVICE_NAME     "SmowCode-SensorHub"

/* ── GATT Service / Characteristic UUIDs ───────────────────────────────────── */
/* Environmental Sensing Service: 0x181A */
#define ENV_SENSING_SVC_UUID        0x181A
/* Temperature: 0x2A6E, Humidity: 0x2A6F */
#define CHAR_UUID_TEMPERATURE       0x2A6E
#define CHAR_UUID_HUMIDITY          0x2A6F
/* Custom accelerometer (SmowCode-specific) */
#define CHAR_UUID_ACCEL             0xFF01

#define GATTS_APP_ID                0
#define GATTS_NUM_HANDLE            8

static uint16_t s_gatts_if        = ESP_GATT_IF_NONE;
static uint16_t s_service_handle  = 0;
static uint16_t s_conn_id         = 0xFFFF;
static bool     s_connected       = false;
static bool     s_notify_enabled  = false;

/* ── Simulated Sensor Readings ──────────────────────────────────────────────── */
static int16_t  s_temperature_x100 = 2250;  /* 22.50 °C */
static uint16_t s_humidity_x100    = 5500;  /* 55.00 % */
static int16_t  s_accel_x          = 0;
static int16_t  s_accel_y          = 0;
static int16_t  s_accel_z          = 1000;  /* ~1g downward */

/* ── GAP Advertising Data ───────────────────────────────────────────────────── */
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x0540,   /* Generic Sensor */
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ── GAP Event Handler ──────────────────────────────────────────────────────── */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        esp_ble_gap_start_advertising(&s_adv_params);
    } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        ESP_LOGI(TAG, "BLE advertising started");
    }
}

/* ── GATT Server Event Handler ──────────────────────────────────────────────── */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                  esp_gatt_if_t gatts_if,
                                  esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            s_gatts_if = gatts_if;
            esp_ble_gap_set_device_name(DEVICE_NAME);
            esp_ble_gap_config_adv_data(&s_adv_data);
            ESP_LOGI(TAG, "GATT server registered, if=%d", gatts_if);
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_id   = param->connect.conn_id;
            s_connected = true;
            ESP_LOGI(TAG, "Client connected: conn_id=%d", s_conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_connected      = false;
            s_notify_enabled = false;
            ESP_LOGI(TAG, "Client disconnected — restarting advertising");
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            /* Check if client enabled notifications (CCCD write) */
            if (param->write.len == 2) {
                uint16_t cccd = param->write.value[0] | (param->write.value[1] << 8);
                s_notify_enabled = (cccd & 0x0001);
                ESP_LOGI(TAG, "Notifications %s", s_notify_enabled ? "ENABLED" : "DISABLED");
            }
            break;

        default:
            break;
    }
}

/* ── Sensor Notification Task ───────────────────────────────────────────────── */
static void sensor_notify_task(void *pvParameters)
{
    while (1) {
        if (s_connected && s_notify_enabled && s_gatts_if != ESP_GATT_IF_NONE) {
            /* Simulate sensor drift */
            s_temperature_x100 += (esp_random() % 10) - 5;
            s_humidity_x100    += (esp_random() % 6)  - 3;
            s_accel_x = (int16_t)(esp_random() % 100) - 50;
            s_accel_y = (int16_t)(esp_random() % 100) - 50;

            /* Send temperature notification (int16, units of 0.01 °C) */
            esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                         s_service_handle + 2,
                                         sizeof(s_temperature_x100),
                                         (uint8_t *)&s_temperature_x100,
                                         false);

            ESP_LOGI(TAG, "Notify: temp=%.2f°C  hum=%.2f%%  accel=[%d,%d,%d]",
                     s_temperature_x100 / 100.0f,
                     s_humidity_x100 / 100.0f,
                     s_accel_x, s_accel_y, s_accel_z);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ── app_main ───────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 BLE Sensor Hub ===");
    ESP_LOGI(TAG, "Device name: %s", DEVICE_NAME);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    xTaskCreate(sensor_notify_task, "ble_notify", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "BLE initialisation complete");
}
