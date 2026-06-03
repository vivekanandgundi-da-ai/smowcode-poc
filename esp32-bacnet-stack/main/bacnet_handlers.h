#pragma once
#include <stdint.h>
#include "lwip/sockets.h"

void bacnet_handlers_init(void);
void bacnet_handlers_process(int sock, const uint8_t *buf, int len,
                              const struct sockaddr_in *sender);
