idf_component_register(
    SRCS "ds18b20.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        espressif__onewire_bus
        espressif__ds18b20
)
