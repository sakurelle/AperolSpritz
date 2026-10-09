#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
class MeasurementController;
class WifiManager;
class WebServer;

struct LoggerSnapshot {
    bool connected = false;
    char client_name[48]{};
};

struct WebContext {
    MeasurementController *controller = nullptr;
    WifiManager *wifi = nullptr;
    WebServer *server = nullptr;
};

class WebServer {
public:
    esp_err_t start(MeasurementController *controller, WifiManager *wifi);
    void record_logger_heartbeat(const char *client_name);
    LoggerSnapshot logger_snapshot() const;

private:
    httpd_handle_t server_ = nullptr;
    WebContext context_{};
    mutable portMUX_TYPE logger_lock_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t last_logger_heartbeat_ms_ = 0;
    char logger_client_name_[48]{};
};
