#include "config/config_storage.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "config_storage";
static constexpr char NS[] = "needle_meter";

// The v4 layout is retained solely to migrate existing NVS blobs without
// discarding the operator's settings when mm_per_step was introduced.
struct DeviceConfigV4 {
    uint32_t config_version;
    double calibration_length_mm;
    double auto_calibration_length_mm;
    double nominal_length_mm;
    double tolerance_mm;
    double retract_mm;
    bool measure_dir_inverted;
};

// v5 added mm_per_step. Keep the exact old binary layout so deployed devices
// retain every operator setting when v6 adds the calibration interval.
struct DeviceConfigV5 {
    uint32_t config_version;
    double calibration_length_mm;
    double auto_calibration_length_mm;
    double nominal_length_mm;
    double tolerance_mm;
    double retract_mm;
    bool measure_dir_inverted;
    double mm_per_step;
};

// DeviceStats is stored as a blob too. v6 adds a dedicated AUTO-only counter;
// migrate the existing statistics rather than silently discarding them.
struct DeviceStatsV5 {
    double last_measured_length;
    double last_deviation;
    uint32_t measurements_since_calibration;
    uint32_t total_measurements;
    uint32_t calibration_count;
    uint32_t last_measurement_steps;
    uint32_t calibration_steps;
    int64_t calibration_contact_position_steps;
    int64_t last_measurement_contact_position_steps;
    double calibration_reference_length_mm;
    CalibrationSource calibration_source;
    bool calibration_valid;
    char last_result[12];
};

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
    bool migrated = false;
    bool stats_migrated = false;
    nvs_handle_t handle; esp_err_t err = nvs_open(NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { used_defaults = true; return ESP_OK; }
    if (err != ESP_OK) return err;
    size_t size = 0;
    err = nvs_get_blob(handle, "config", nullptr, &size);
    if (err == ESP_OK && size == sizeof(config)) {
        err = nvs_get_blob(handle, "config", &config, &size);
        if (err != ESP_OK || !validate_config(config)) { config = default_config(); used_defaults = true; }
    } else if (err == ESP_OK && size == sizeof(DeviceConfigV5)) {
        DeviceConfigV5 legacy{};
        err = nvs_get_blob(handle, "config", &legacy, &size);
        if (err == ESP_OK && legacy.config_version == 5) {
            config = default_config();
            config.calibration_length_mm = legacy.calibration_length_mm;
            config.auto_calibration_length_mm = legacy.auto_calibration_length_mm;
            config.nominal_length_mm = legacy.nominal_length_mm;
            config.tolerance_mm = legacy.tolerance_mm;
            config.retract_mm = legacy.retract_mm;
            config.measure_dir_inverted = legacy.measure_dir_inverted;
            config.mm_per_step = legacy.mm_per_step;
            config.auto_calibration_interval = 1;
            if (!validate_config(config)) { config = default_config(); used_defaults = true; }
            else { migrated = true; ESP_LOGI(TAG, "Migrated configuration v5 to v6"); }
        } else { config = default_config(); used_defaults = true; }
    } else if (err == ESP_OK && size == sizeof(DeviceConfigV4)) {
        DeviceConfigV4 legacy{};
        err = nvs_get_blob(handle, "config", &legacy, &size);
        if (err == ESP_OK && legacy.config_version == 4) {
            config = default_config();
            config.calibration_length_mm = legacy.calibration_length_mm;
            config.auto_calibration_length_mm = legacy.auto_calibration_length_mm;
            config.nominal_length_mm = legacy.nominal_length_mm;
            config.tolerance_mm = legacy.tolerance_mm;
            config.retract_mm = legacy.retract_mm;
            config.measure_dir_inverted = legacy.measure_dir_inverted;
            if (!validate_config(config)) { config = default_config(); used_defaults = true; }
            else { migrated = true; ESP_LOGI(TAG, "Migrated configuration v4 to v6"); }
        } else { config = default_config(); used_defaults = true; }
    } else { config = default_config(); used_defaults = true; }
    size = 0;
    err = nvs_get_blob(handle, "stats", nullptr, &size);
    if (err == ESP_OK && size == sizeof(stats)) {
        err = nvs_get_blob(handle, "stats", &stats, &size);
        if (err != ESP_OK || stats.stats_version != CONFIG_VERSION) stats = DeviceStats{};
    } else if (err == ESP_OK && size == sizeof(DeviceStatsV5)) {
        DeviceStatsV5 legacy{};
        err = nvs_get_blob(handle, "stats", &legacy, &size);
        if (err == ESP_OK) {
            stats.last_measured_length = legacy.last_measured_length;
            stats.last_deviation = legacy.last_deviation;
            stats.measurements_since_calibration = legacy.measurements_since_calibration;
            stats.auto_measurements_since_calibration = 0;
            stats.total_measurements = legacy.total_measurements;
            stats.calibration_count = legacy.calibration_count;
            stats.last_measurement_steps = legacy.last_measurement_steps;
            stats.calibration_steps = legacy.calibration_steps;
            stats.calibration_contact_position_steps = legacy.calibration_contact_position_steps;
            stats.last_measurement_contact_position_steps = legacy.last_measurement_contact_position_steps;
            stats.calibration_reference_length_mm = legacy.calibration_reference_length_mm;
            stats.calibration_source = legacy.calibration_source;
            stats.calibration_valid = legacy.calibration_valid;
            std::memcpy(stats.last_result, legacy.last_result, sizeof(stats.last_result));
            stats_migrated = true;
        } else stats = DeviceStats{};
    } else stats = DeviceStats{};
    stats.last_result[sizeof(stats.last_result) - 1] = '\0';
    nvs_close(handle);
    // The migrated v6 config is immediately persisted. A subsequent reboot
    // therefore no longer depends on the legacy binary layout.
    if (migrated) {
        // A successful v4 migration must be durable before the next reboot.
        if (save_config(config) != ESP_OK) ESP_LOGW(TAG, "Could not persist configuration migration");
    }
    if (stats_migrated && save_stats(stats) != ESP_OK) ESP_LOGW(TAG, "Could not persist statistics migration");
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
