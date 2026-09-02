#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
class MeasurementController;
class WifiManager;

struct WebContext {
    MeasurementController *controller = nullptr;
    WifiManager *wifi = nullptr;
};

class WebServer {
public:
    esp_err_t start(MeasurementController *controller, WifiManager *wifi);

private:
    httpd_handle_t server_ = nullptr;
    WebContext context_{};
};
