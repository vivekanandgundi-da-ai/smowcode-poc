/**
 * @file modbus_main.c
 * @brief ESP32 Modbus RTU Master — SmowCode Sample Project
 *
 * Implements a Modbus RTU master over RS-485 using UART2.
 * Reads holding registers from a Modbus slave device every 2 seconds.
 *
 * Features:
 *   - Modbus RTU (RS-485) master
 *   - Function Code 03: Read Holding Registers
 *   - Function Code 06: Write Single Register
 *   - CRC16 validation
 *   - RS-485 direction control via DE/RE pin
 *
 * Wiring (RS-485 transceiver e.g. MAX485):
 *   GPIO16 (UART2 TX)  →  DI (Driver Input)
 *   GPIO17 (UART2 RX)  ←  RO (Receiver Output)
 *   GPIO18             →  DE + RE (direction control, HIGH=TX, LOW=RX)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "modbus_main";

/* ── Hardware Configuration ─────────────────────────────────────────────────── */
#define MB_UART_PORT    UART_NUM_2
#define MB_TX_GPIO      GPIO_NUM_16
#define MB_RX_GPIO      GPIO_NUM_17
#define MB_DE_RE_GPIO   GPIO_NUM_18     /* RS-485 direction control */
#define MB_BAUD_RATE    9600
#define MB_BUF_SIZE     256

/* ── Modbus Constants ───────────────────────────────────────────────────────── */
#define MB_FC_READ_HOLDING_REGS     0x03
#define MB_FC_WRITE_SINGLE_REG      0x06
#define MB_SLAVE_ADDR               1       /* Target slave device address */
#define MB_START_REG                0x0000  /* First register to read */
#define MB_NUM_REGS                 4       /* Number of registers to read */

/* ── CRC16 (Modbus) ─────────────────────────────────────────────────────────── */
static uint16_t modbus_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ── RS-485 Direction Control ───────────────────────────────────────────────── */
static inline void rs485_tx_mode(void) { gpio_set_level(MB_DE_RE_GPIO, 1); }
static inline void rs485_rx_mode(void) { gpio_set_level(MB_DE_RE_GPIO, 0); }

/* ── UART Init ──────────────────────────────────────────────────────────────── */
static void uart_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate  = MB_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_driver_install(MB_UART_PORT, MB_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MB_UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(MB_UART_PORT, MB_TX_GPIO, MB_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* DE/RE GPIO for RS-485 direction */
    gpio_set_direction(MB_DE_RE_GPIO, GPIO_MODE_OUTPUT);
    rs485_rx_mode();

    ESP_LOGI(TAG, "UART2 initialised: %d baud, RS-485 DE/RE on GPIO%d",
             MB_BAUD_RATE, MB_DE_RE_GPIO);
}

/* ── Send Modbus RTU Request ────────────────────────────────────────────────── */
static int modbus_send_request(uint8_t slave, uint8_t fc,
                                uint16_t reg_addr, uint16_t value_or_count)
{
    uint8_t req[8];
    req[0] = slave;
    req[1] = fc;
    req[2] = (reg_addr >> 8) & 0xFF;
    req[3] =  reg_addr       & 0xFF;
    req[4] = (value_or_count >> 8) & 0xFF;
    req[5] =  value_or_count       & 0xFF;

    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    rs485_tx_mode();
    uart_write_bytes(MB_UART_PORT, req, sizeof(req));
    uart_wait_tx_done(MB_UART_PORT, pdMS_TO_TICKS(50));
    rs485_rx_mode();

    ESP_LOGD(TAG, "TX: slave=%d fc=0x%02X reg=0x%04X val/cnt=%d",
             slave, fc, reg_addr, value_or_count);
    return sizeof(req);
}

/* ── Read Modbus Response ───────────────────────────────────────────────────── */
static int modbus_read_response(uint8_t *buf, int max_len, int timeout_ms)
{
    int len = uart_read_bytes(MB_UART_PORT, buf, max_len, pdMS_TO_TICKS(timeout_ms));
    if (len <= 0) {
        ESP_LOGW(TAG, "No response from slave (timeout %dms)", timeout_ms);
        return -1;
    }

    /* Validate CRC */
    if (len >= 4) {
        uint16_t recv_crc = buf[len-1] << 8 | buf[len-2];
        uint16_t calc_crc = modbus_crc16(buf, len - 2);
        if (recv_crc != calc_crc) {
            ESP_LOGE(TAG, "CRC mismatch: received=0x%04X calculated=0x%04X",
                     recv_crc, calc_crc);
            return -2;
        }
    }

    return len;
}

/* ── Modbus Master Task ─────────────────────────────────────────────────────── */
static void modbus_master_task(void *pvParameters)
{
    uint8_t resp[MB_BUF_SIZE];

    while (1) {
        /* Read 4 holding registers from slave 1 starting at register 0 */
        modbus_send_request(MB_SLAVE_ADDR, MB_FC_READ_HOLDING_REGS,
                             MB_START_REG, MB_NUM_REGS);

        int len = modbus_read_response(resp, sizeof(resp), 200);
        if (len > 0) {
            /* FC03 response: [slave][0x03][byte_count][data...][crc_lo][crc_hi] */
            uint8_t byte_count = resp[2];
            uint8_t num_regs = byte_count / 2;
            ESP_LOGI(TAG, "Slave %d response: %d registers", MB_SLAVE_ADDR, num_regs);

            for (int i = 0; i < num_regs; i++) {
                uint16_t reg_val = (resp[3 + i*2] << 8) | resp[4 + i*2];
                ESP_LOGI(TAG, "  Register[%d] = %u (0x%04X)",
                         MB_START_REG + i, reg_val, reg_val);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ── app_main ───────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== SmowCode ESP32 Modbus RTU Master ===");
    ESP_LOGI(TAG, "Slave addr: %d  Baud: %d  RS-485 GPIO: TX=%d RX=%d DE/RE=%d",
             MB_SLAVE_ADDR, MB_BAUD_RATE, MB_TX_GPIO, MB_RX_GPIO, MB_DE_RE_GPIO);

    uart_init();

    xTaskCreate(modbus_master_task, "modbus_master", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Modbus master task started");
}
