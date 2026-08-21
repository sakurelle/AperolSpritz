#include "driver/gpio.h"
#include "esp_log.h"
#include "config/config_storage.hpp"
#include "gpio/gpio_manager.hpp"
#include "motor/stepper_motor.hpp"
#include "measurement/measurement_controller.hpp"
#include "wifi/wifi_manager.hpp"
#include "web/web_server.hpp"

static const char *TAG = "app";
// app_main returns after startup, so application services must not be stack objects.
static ConfigStorage storage;
static StepperMotor motor;
static GpioManager gpio;
static MeasurementController controller;
static WifiManager wifi;
static WebServer web;

extern "C" void app_main(void) {
    // Put the driver in a safe electrical state before NVS, Wi-Fi, or any task can run.
    gpio_config_t safety{};
    safety.pin_bit_mask = (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4) | (1ULL << GPIO_NUM_10);
    safety.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&safety));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_3, 0));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_10, 1)); // TMC2209 EN is active LOW

    if (storage.init() != ESP_OK) { ESP_LOGE(TAG, "NVS initialization failed; motor remains disabled"); return; }
    DeviceConfig config; DeviceStats stats; bool defaults = false;
    if (storage.load(config, stats, defaults) != ESP_OK || !validate_config(config)) { ESP_LOGE(TAG, "Invalid NVS configuration; motor remains disabled"); return; }
    if (defaults) { ESP_LOGW(TAG, "Using factory defaults"); storage.save_config(config); storage.save_stats(stats); }

    if (motor.init() != ESP_OK) { ESP_LOGE(TAG, "Motor initialization failed"); return; }
    if (controller.init(&motor, &gpio, &storage, config, stats) != ESP_OK) { ESP_LOGE(TAG, "GPIO/controller initialization failed"); motor.stop(); return; }
    if (controller.start_task() != ESP_OK) { ESP_LOGE(TAG, "Controller task initialization failed"); motor.stop(); return; }

    if (wifi.start_access_point() != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi AP failed; physical controls remain active"); return; }
    if (web.start(&controller) != ESP_OK) ESP_LOGE(TAG, "HTTP server unavailable; physical controls remain active");
}
