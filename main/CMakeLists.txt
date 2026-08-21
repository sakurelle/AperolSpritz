idf_component_register(
    SRCS "app_main.cpp"
         "gpio/gpio_manager.cpp"
         "motor/stepper_motor.cpp"
         "measurement/measurement_controller.cpp"
         "config/device_config.cpp"
         "config/config_storage.cpp"
         "web/web_server.cpp"
         "wifi/wifi_manager.cpp"
    INCLUDE_DIRS "." "gpio" "motor" "measurement" "config" "web" "wifi"
    REQUIRES esp_driver_gpio esp_driver_gptimer esp_timer esp_event esp_netif esp_http_server esp_wifi nvs_flash)
