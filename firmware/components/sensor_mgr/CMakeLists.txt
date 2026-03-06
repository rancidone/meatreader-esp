idf_component_register(
    SRCS "sensor_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES ads1115 config_mgr therm_math board freertos esp_timer
)
