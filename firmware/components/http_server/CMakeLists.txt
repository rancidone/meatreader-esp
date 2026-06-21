set(UI_DIST    "${CMAKE_SOURCE_DIR}/../thermometer-ui/dist")
set(UI_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui_gen")
set(UI_ASSETS_C "${UI_GEN_DIR}/ui_assets.c")

# Must exist at configure time so idf_component_register can validate INCLUDE_DIRS.
file(MAKE_DIRECTORY "${UI_GEN_DIR}")

# Register component first — ESP-IDF script mode only processes this call.
# The generated ui_assets.c is added as a source after registration.
idf_component_register(
    SRCS
        "http_server.c"
        "routes_temps.c"
        "routes_config.c"
        "routes_calibration.c"
        "routes_status.c"
        "routes_metrics.c"
        "routes_provision.c"
        "routes_alerts.c"
        "routes_dashboard.c"
        "routes_events.c"
        "routes_profiles.c"
        "routes_push.c"
        "routes_ota.c"
        "routes_static.c"
    INCLUDE_DIRS
        "include"
        "${UI_GEN_DIR}"
    REQUIRES
        esp_http_server
        esp_http_client
        sensor_mgr
        config_mgr
        calibration
        alert_mgr
        ref_sensor
        esp_timer
        esp_wifi
        nvs_flash
        app_update
        freertos
        espressif__cjson
)

# Custom commands are not available in ESP-IDF's early-expansion script mode.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)

    # Step 1: always run npm build (target with no OUTPUT runs every time)
    add_custom_target(ui_svelte_build ALL
        COMMAND npm --prefix "${CMAKE_SOURCE_DIR}/../thermometer-ui" run build
        COMMENT "Building Svelte UI"
        VERBATIM
    )

    # Step 2: gzip + emit ui_assets.c — always regenerate after the npm build
    add_custom_target(ui_assets_gen ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_GEN_DIR}"
        COMMAND python3
            "${CMAKE_SOURCE_DIR}/scripts/gen_ui_assets.py"
            "${UI_DIST}"
            "${UI_ASSETS_C}"
        DEPENDS ui_svelte_build
        COMMENT "Generating ui_assets.c"
        VERBATIM
    )

    # Wire the generated source into the component target and ensure ordering.
    target_sources(__idf_http_server PRIVATE "${UI_ASSETS_C}")
    add_dependencies(__idf_http_server ui_assets_gen)

endif()
