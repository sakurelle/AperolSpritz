#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "driver/gpio.h"
class StepperMotor;

enum class GpioEventType : uint8_t { MeasurePressed, CalibratePressed, LeftChanged, ContactActive, StopActive };
struct GpioEvent { GpioEventType type; bool level_low; uint32_t timestamp_ms; };

class GpioManager {
public:
    esp_err_t init(QueueHandle_t event_queue, StepperMotor *motor);
    bool needle_present() const;
    bool contact_active() const;
    bool stop_active() const;
    bool left_pressed() const;
private:
    static void IRAM_ATTR gpio_isr(void *arg);
    struct IsrContext { GpioManager *owner; gpio_num_t pin; };
    QueueHandle_t queue_ = nullptr;
    StepperMotor *motor_ = nullptr;
    IsrContext contexts_[5]{};
};
