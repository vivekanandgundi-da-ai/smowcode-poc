#include "bacnet_device.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "bacnet_device";

static uint32_t s_device_id = 0;
static char     s_device_name[64] = {0};
static uint32_t s_ip_addr = 0;

void bacnet_device_init(uint32_t device_id, const char *device_name)
{
    s_device_id = device_id;
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    ESP_LOGI(TAG, "Device initialised: id=%u name=%s", device_id, device_name);
}

void bacnet_device_set_ip(uint32_t ip_addr)
{
    s_ip_addr = ip_addr;
}

uint32_t bacnet_device_get_id(void)
{
    return s_device_id;
}
