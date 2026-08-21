#include "config/config_storage.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "config_storage";
static constexpr char NS[] = "needle_meter";

esp_err_t ConfigStorage::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t ConfigStorage::load(DeviceConfig &config, DeviceStats &stats, bool &used_defaults) {
    used_defaults = false; config = default_config(); stats = DeviceStats{};
    nvs_handle_t handle; esp_err_t err = nvs_open(NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { used_defaults = true; return ESP_OK; }
    if (err != ESP_OK) return err;
    size_t size = sizeof(config); err = nvs_get_blob(handle, "config", &config, &size);
    if (err != ESP_OK || size != sizeof(config) || !validate_config(config)) { config = default_config(); used_defaults = true; }
    size = sizeof(stats); err = nvs_get_blob(handle, "stats", &stats, &size);
    if (err != ESP_OK || size != sizeof(stats)) stats = DeviceStats{};
    stats.last_result[sizeof(stats.last_result) - 1] = '\0';
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t ConfigStorage::save_config(const DeviceConfig &config) {
    if (!validate_config(config)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t err = nvs_open(NS, NVS_READWRITE, &h); if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "config", &config, sizeof(config)); if (err == ESP_OK) err = nvs_commit(h); nvs_close(h); return err;
}
esp_err_t ConfigStorage::save_stats(const DeviceStats &stats) {
    nvs_handle_t h; esp_err_t err = nvs_open(NS, NVS_READWRITE, &h); if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "stats", &stats, sizeof(stats)); if (err == ESP_OK) err = nvs_commit(h); nvs_close(h); return err;
}
esp_err_t ConfigStorage::factory_reset(DeviceConfig &config, DeviceStats &stats) {
    nvs_handle_t h; esp_err_t err = nvs_open(NS, NVS_READWRITE, &h); if (err != ESP_OK) return err;
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h); nvs_close(h);
    if (err == ESP_OK) { config = default_config(); stats = DeviceStats{}; ESP_LOGI(TAG, "Factory settings restored"); }
    return err;
}
