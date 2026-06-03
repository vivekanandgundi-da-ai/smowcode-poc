idf_component_register(
    SRCS "modbus_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_log nvs_flash
)
