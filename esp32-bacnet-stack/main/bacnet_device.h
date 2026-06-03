#pragma once
#include <stdint.h>

void bacnet_device_init(uint32_t device_id, const char *device_name);
void bacnet_device_set_ip(uint32_t ip_addr);
uint32_t bacnet_device_get_id(void);
