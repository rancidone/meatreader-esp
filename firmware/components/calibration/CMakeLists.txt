idf_component_register(
    SRCS "calibration.c"
    INCLUDE_DIRS "include"
    REQUIRES config_mgr therm_math board
)
