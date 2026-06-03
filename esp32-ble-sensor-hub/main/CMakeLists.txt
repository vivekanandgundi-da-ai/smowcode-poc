idf_component_register(
    SRCS "ble_main.c"
    INCLUDE_DIRS "."
    REQUIRES bt esp_log nvs_flash
)
