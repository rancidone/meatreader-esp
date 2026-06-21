idf_component_register(
    SRCS "sensor_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES ads1115 config_mgr therm_math board i2c_manager freertos esp_timer esp_system
)
