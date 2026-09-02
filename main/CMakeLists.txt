set(GENERATED_INCLUDE_DIR "")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(WIFI_SECRETS_CONFIGURED 0)
    set(WIFI_AUTH_ESCAPED "")
    set(WIFI_SSID_ESCAPED "")
    set(WIFI_IDENTITY_ESCAPED "")
    set(WIFI_USERNAME_ESCAPED "")
    set(WIFI_PASSWORD_ESCAPED "")
    set(SECRETS_FILE "${CMAKE_SOURCE_DIR}/secrets.txt")
    if(EXISTS "${SECRETS_FILE}")
        file(STRINGS "${SECRETS_FILE}" SECRETS_LINES)
        foreach(line IN LISTS SECRETS_LINES)
            if(line MATCHES "^WIFI_(AUTH|SSID|IDENTITY|USERNAME|PASSWORD)=(.*)$")
                set(key "WIFI_${CMAKE_MATCH_1}")
                set(value "${CMAKE_MATCH_2}")
                string(REPLACE "\\" "\\\\" value "${value}")
                string(REPLACE "\"" "\\\"" value "${value}")
                set("${key}_ESCAPED" "${value}")
            endif()
        endforeach()
        if(NOT WIFI_AUTH_ESCAPED STREQUAL "" AND NOT WIFI_SSID_ESCAPED STREQUAL "" AND NOT WIFI_PASSWORD_ESCAPED STREQUAL "")
            set(WIFI_SECRETS_CONFIGURED 1)
        endif()
    endif()
    set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/config/wifi_secrets.hpp.in" "${GENERATED_INCLUDE_DIR}/wifi_secrets.hpp" @ONLY)
endif()

idf_component_register(
    SRCS "app_main.cpp"
         "gpio/gpio_manager.cpp"
         "motor/stepper_motor.cpp"
         "measurement/measurement_controller.cpp"
         "config/device_config.cpp"
         "config/config_storage.cpp"
         "web/web_server.cpp"
         "wifi/wifi_manager.cpp"
    INCLUDE_DIRS "." "gpio" "motor" "measurement" "config" "web" "wifi" "${GENERATED_INCLUDE_DIR}"
    REQUIRES esp_driver_gpio esp_driver_gptimer esp_timer esp_event esp_netif esp_http_server esp_wifi nvs_flash wpa_supplicant)
