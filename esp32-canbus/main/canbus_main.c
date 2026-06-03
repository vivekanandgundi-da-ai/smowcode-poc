/**
 * @file canbus_main.c
 * @brief ESP32 CAN Bus (TWAI) Interface — SmowCode Sample Project
 *
 * Uses the ESP32 built-in TWAI (Two-Wire Automotive Interface) controller.
 * Compatible with CAN 2.0A (11-bit ID) and CAN 2.0B (29-bit extended ID).
 *
 * Features:
 *   - 500 Kbps CAN bus speed
 *   - Transmit and receive standard/extended frames
 *   - Error passive / bus-off detection and recovery
 *   - Simple OBD-II PID request example (Mode 01, PID 0C = Engine RPM)
 *
 * Wiring:
 *   GPIO4  → CAN TX (to CAN transceiver TXD)
 *   GPIO5  → CAN RX (from CAN transceiver RXD)
 *   Use SN65HVD230 or MCP2562 transceiver between ESP32 and CAN bus.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"

static const char *TAG = "canbus_main";

/* ── Pin and Speed Configuration ───────────────────────────────────────────── */
#define CAN_TX_GPIO     GPIO_NUM_4
#define CAN_RX_GPIO     GPIO_NUM_5
#define CAN_SPEED       TWAI_TIMING_CONFIG_500KBITS()

/* OBD-II example: request engine RPM (Mode 01 PID 0C) */
#define OBD_REQUEST_ID      0x7DF   /* Broadcast functional request */
#define OBD_RESPONSE_ID     0x7E8   /* ECU response */
#define OBD_PID_ENGINE_RPM  0x0C

/* ── TWAI Initialisation ────────────────────────────────────────────────────── */
static esp_err_t twai_init(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO,
                                                                   TWAI_MODE_NORMAL);
    twai_timing_config_t  t_config = CAN_SPEED;
    twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TWAI (CAN) driver started at 500 Kbps");
    return ESP_OK;
}

/* ── Send CAN Frame ─────────────────────────────────────────────────────────── */
static esp_err_t can_send_frame(uint32_t id, const uint8_t *data, uint8_t dlc, bool extended)
{
    twai_message_t msg = {
        .identifier       = id,
        .data_length_code = dlc,
        .extd             = extended ? 1 : 0,
        .rtr              = 0,
        .ss               = 0,
    };
    memcpy(msg.data, data, dlc);

    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "TX id=0x%03X dlc=%d data=%02X %02X %02X %02X",
                 id, dlc, data[0], data[1], data[2], data[3]);
    } else {
        ESP_LOGW(TAG, "TX failed id=0x%03X: %s", id, esp_err_to_name(ret));
    }
    return ret;
}

/* ── Receive CAN Frame ──────────────────────────────────────────────────────── */
static void can_receive_task(void *pvParameters)
{
    twai_message_t msg;

    while (1) {
        esp_err_t ret = twai_receive(&msg, pdMS_TO_TICKS(500));
        if (ret == ESP_OK) {
            if (msg.extd) {
                ESP_LOGI(TAG, "RX ext id=0x%08X dlc=%d", msg.identifier, msg.data_length_code);
            } else {
                ESP_LOGI(TAG, "RX std id=0x%03X dlc=%d", msg.identifier, msg.data_length_code);
            }

            /* Log raw data */
            char hex[32] = {0};
            for (int i = 0; i < msg.data_length_code && i < 8; i++) {
                snprintf(hex + i * 3, 4, "%02X ", msg.data[i]);
            }
            ESP_LOGI(TAG, "  data: %s", hex);

            /* Parse OBD-II RPM response */
            if (msg.identifier == OBD_RESPONSE_ID && msg.data_length_code >= 4
                && msg.data[2] == 0x41 && msg.data[3] == OBD_PID_ENGINE_RPM) {
                uint16_t rpm = ((msg.data[4] * 256) + msg.data[5]) / 4;
                ESP_LOGI(TAG, "  Engine RPM: %u", rpm);
            }
        }

        /* Check for bus errors */
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (status.state == TWAI_STATE_BUS_OFF) {
                ESP_LOGE(TAG, "CAN bus-off — initiating recovery");
                twai_initiate_recovery();
            }
        }
    }
}

/* ── OBD-II Request Task ────────────────────────────────────────────────────── */
static void obd_request_task(void *pvParameters)
{
    /* OBD-II Mode 01 PID 0C: Engine RPM */
    uint8_t obd_rpm_request[] = {0x02, 0x01, OBD_PID_ENGINE_RPM, 0x00, 0x00, 0x00, 0x00, 0x00};

    while (1) {
        ESP_LOGI(TAG, "Sending OBD-II RPM request");
        can_send_frame(OBD_REQUEST_ID, obd_rpm_request, 8, false);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── app_main ───────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 CAN Bus Interface ===");
    ESP_LOGI(TAG, "TX GPIO: %d  RX GPIO: %d  Speed: 500 Kbps", CAN_TX_GPIO, CAN_RX_GPIO);

    if (twai_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise TWAI — halting");
        return;
    }

    /* Start RX task */
    xTaskCreate(can_receive_task, "can_rx", 4096, NULL, 6, NULL);

    /* Start OBD-II request task (sends RPM query every 1 second) */
    xTaskCreate(obd_request_task, "obd_req", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "CAN bus tasks started");
}
