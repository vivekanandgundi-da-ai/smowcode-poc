#include "bacnet_handlers.h"
#include "bacnet_device.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bacnet_handlers";

/* BACnet/IP BVLC function codes */
#define BVLC_ORIGINAL_UNICAST_NPDU      0x0A
#define BVLC_ORIGINAL_BROADCAST_NPDU    0x0B
#define BVLC_TYPE                       0x81

/* BACnet service choice codes */
#define SERVICE_CONFIRMED_READ_PROPERTY     12
#define SERVICE_CONFIRMED_WRITE_PROPERTY    15
#define SERVICE_UNCONFIRMED_WHO_IS          8
#define SERVICE_UNCONFIRMED_I_AM            0

void bacnet_handlers_init(void)
{
    ESP_LOGI(TAG, "BACnet handlers initialised");
}

/**
 * Send an I-Am response on the BACnet/IP network.
 */
static void send_iam(int sock, const struct sockaddr_in *dest)
{
    /* Minimal I-Am APDU (unconfirmed service) */
    uint32_t device_id = bacnet_device_get_id();
    uint8_t iam_pdu[] = {
        0x81, 0x0B,          /* BVLC type + Original-Broadcast-NPDU */
        0x00, 0x1B,          /* BVLC length (27 bytes) */
        0x01, 0x00,          /* NPDU version + control */
        0x10,                /* APDU type = Unconfirmed-Request */
        SERVICE_UNCONFIRMED_I_AM,
        /* Object Identifier (Device, device_id) */
        0xC4, 0x02,
        (uint8_t)((device_id >> 16) & 0xFF),
        (uint8_t)((device_id >>  8) & 0xFF),
        (uint8_t)( device_id        & 0xFF),
        /* Max APDU length: 1476 */
        0x22, 0x05, 0xC4,
        /* Segmentation: no segmentation */
        0x91, 0x00,
        /* Vendor ID: 0 (ASHRAE) */
        0x21, 0x00,
    };

    struct sockaddr_in broadcast = {
        .sin_family      = AF_INET,
        .sin_port        = dest->sin_port,
        .sin_addr.s_addr = INADDR_BROADCAST,
    };

    sendto(sock, iam_pdu, sizeof(iam_pdu), 0,
           (struct sockaddr *)&broadcast, sizeof(broadcast));
    ESP_LOGI(TAG, "Sent I-Am for device %u", device_id);
}

void bacnet_handlers_process(int sock, const uint8_t *buf, int len,
                              const struct sockaddr_in *sender)
{
    if (len < 6) return;

    /* Validate BVLC header */
    if (buf[0] != BVLC_TYPE) return;

    /* Skip BVLC header (4 bytes) and NPDU header (2 bytes) */
    if (len < 8) return;
    const uint8_t *apdu = buf + 6;
    int apdu_len = len - 6;

    uint8_t apdu_type = (apdu[0] >> 4) & 0x0F;

    if (apdu_type == 0x1) {
        /* Unconfirmed Request */
        uint8_t service = apdu[1];
        if (service == SERVICE_UNCONFIRMED_WHO_IS) {
            ESP_LOGI(TAG, "Received Who-Is — sending I-Am");
            send_iam(sock, sender);
        }
    } else if (apdu_type == 0x0) {
        /* Confirmed Request */
        uint8_t service = apdu[3];
        ESP_LOGI(TAG, "Confirmed request: service=%d (not yet implemented in PoC)", service);
    }
}
