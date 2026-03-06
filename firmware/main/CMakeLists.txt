idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        i2c_manager
        ads1115
        ref_sensor
        config_mgr
        sensor_mgr
        calibration
        wifi_mgr
        http_server
        nvs_flash
        esp_netif
        esp_event
)
