#include "gpio/gpio_manager.hpp"
#include "motor/stepper_motor.hpp"
#include "driver/gpio.h"
#include "esp_check.h"

namespace { constexpr gpio_num_t MEASURE_PIN=GPIO_NUM_0, STOP_PIN=GPIO_NUM_1, LEFT_PIN=GPIO_NUM_2, NEEDLE_PIN=GPIO_NUM_8, CONTACT_PIN=GPIO_NUM_20, CALIBRATE_PIN=GPIO_NUM_21; }

esp_err_t GpioManager::init(QueueHandle_t queue, StepperMotor *motor) {
    queue_ = queue; motor_ = motor;
    gpio_config_t input{}; input.pin_bit_mask = (1ULL<<MEASURE_PIN)|(1ULL<<STOP_PIN)|(1ULL<<LEFT_PIN)|(1ULL<<NEEDLE_PIN)|(1ULL<<CONTACT_PIN)|(1ULL<<CALIBRATE_PIN);
    input.mode = GPIO_MODE_INPUT; input.pull_up_en = GPIO_PULLUP_ENABLE; input.pull_down_en = GPIO_PULLDOWN_DISABLE; input.intr_type = GPIO_INTR_ANYEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&input), "gpio", "input setup failed");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_IRAM), "gpio", "isr service failed");
    const gpio_num_t interrupt_pins[] = {MEASURE_PIN, STOP_PIN, LEFT_PIN, CONTACT_PIN, CALIBRATE_PIN};
    for (unsigned i = 0; i < 5; ++i) { contexts_[i] = {this, interrupt_pins[i]}; ESP_RETURN_ON_ERROR(gpio_isr_handler_add(interrupt_pins[i], gpio_isr, &contexts_[i]), "gpio", "isr add failed"); }
    return ESP_OK;
}
void IRAM_ATTR GpioManager::gpio_isr(void *arg) {
    auto *ctx = static_cast<IsrContext *>(arg); auto *self = ctx->owner; bool low = gpio_get_level(ctx->pin) == 0;
    GpioEventType type = GpioEventType::LeftChanged;
    if (ctx->pin == STOP_PIN) { type = GpioEventType::StopActive; if (low) self->motor_->emergency_stop_isr(); }
    else if (ctx->pin == CONTACT_PIN) { type = GpioEventType::ContactActive; if (low) self->motor_->request_stop_isr(); }
    else if (ctx->pin == LEFT_PIN) { type = GpioEventType::LeftChanged; if (!low) self->motor_->request_stop_isr(); }
    else if (ctx->pin == MEASURE_PIN) type = GpioEventType::MeasurePressed;
    else if (ctx->pin == CALIBRATE_PIN) type = GpioEventType::CalibratePressed;
    BaseType_t high = pdFALSE; GpioEvent e{type, low, static_cast<uint32_t>(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS)};
    xQueueSendFromISR(self->queue_, &e, &high); if (high) portYIELD_FROM_ISR();
}
bool GpioManager::needle_present() const { return gpio_get_level(NEEDLE_PIN) == 0; }
bool GpioManager::contact_active() const { return gpio_get_level(CONTACT_PIN) == 0; }
bool GpioManager::stop_active() const { return gpio_get_level(STOP_PIN) == 0; }
bool GpioManager::left_pressed() const { return gpio_get_level(LEFT_PIN) == 0; }
