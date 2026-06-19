idf_component_register(
    SRCS "alert_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES
        config_mgr
        sensor_mgr
        esp_http_client
        esp_timer
        freertos
        log
    PRIV_REQUIRES
        espressif__cjson
)
