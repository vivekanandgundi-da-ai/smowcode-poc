idf_component_register(
    SRCS
        "bacnet_main.c"
        "bacnet_device.c"
        "bacnet_handlers.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        esp_event
        lwip
)
