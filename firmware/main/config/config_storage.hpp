#pragma once
#include "config/device_config.hpp"
#include "esp_err.h"

class ConfigStorage {
public:
    esp_err_t init();
    esp_err_t load(DeviceConfig &config, DeviceStats &stats, bool &used_defaults);
    esp_err_t save_config(const DeviceConfig &config);
    esp_err_t save_stats(const DeviceStats &stats);
    esp_err_t factory_reset(DeviceConfig &config, DeviceStats &stats);
};
