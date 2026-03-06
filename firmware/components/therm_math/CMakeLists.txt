idf_component_register(
    SRCS "therm_math.c"
    INCLUDE_DIRS "include"
    # No ESP-IDF component dependencies — pure math, testable in isolation.
)
