idf_component_register(
    SRCS "config_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES therm_math nvs_flash freertos
)
