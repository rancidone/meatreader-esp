idf_component_register(
    SRCS "ads1115.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c_manager driver freertos
)
