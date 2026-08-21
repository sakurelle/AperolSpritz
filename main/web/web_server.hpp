#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
class MeasurementController;
class WebServer { public: esp_err_t start(MeasurementController *controller); private: httpd_handle_t server_ = nullptr; };
