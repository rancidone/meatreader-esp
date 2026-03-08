cmake_minimum_required(VERSION 3.16)

# Minimal stub for esp_log.h to satisfy any includes (therm_math should have none, but be safe)
add_library(therm_math STATIC
  ../../components/therm_math/therm_math.c
)
target_include_directories(therm_math PUBLIC
  ../../components/therm_math/include
  ../../components/board/include
)
target_link_libraries(therm_math m)

add_executable(therm_math_test test_therm_math.c)
target_link_libraries(therm_math_test therm_math m)
add_test(NAME therm_math COMMAND therm_math_test)
