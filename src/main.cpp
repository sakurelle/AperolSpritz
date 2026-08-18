#include <Arduino.h>
#include "Pins.h"
#include "ConfigManager.h"
#include "MotorController.h"
#include "InputManager.h"
#include "MeasurementController.h"
#include "NetworkManager.h"
#include "WebServerManager.h"

ConfigManager configManager;
MotorController motor;
InputManager inputs;
MeasurementController measurement(motor, inputs);
NetworkManager networkManager;
WebServerManager web(configManager, measurement, networkManager, motor, inputs);

static uint8_t priorityOf(InputEvent event) {
  switch (event) {
    case InputEvent::STOP: return 1;
    case InputEvent::LEFT_DOWN: case InputEvent::LEFT_UP: return 4;
    case InputEvent::CALIBRATE: return 5;
    case InputEvent::MEASURE: return 6;
    case InputEvent::NEEDLE_INSERTED: case InputEvent::NEEDLE_REMOVED: return 7;
    default: return 99;
  }
}

void setup() {
  // Drive TMC2209 disable high before any other initialization.
  pinMode(Pins::EN_PIN, OUTPUT); digitalWrite(Pins::EN_PIN, HIGH);
  Serial.begin(115200); delay(50);
  Serial.println("\n[BOOT] NeedleGauge ESP32-C3 starting");
  configManager.begin();
  motor.begin(configManager.get());
  inputs.begin(configManager.get());
  measurement.begin(configManager.get()); // Mechanical reference is never retained across a reboot.
  networkManager.begin(configManager.get());
  web.begin();
}

void loop() {
  // Poll only inexpensive GPIO state before the HTTP server. STOP/contact use ISR and have already stopped STEP.
  inputs.update();
  measurement.update();

  InputEvent selected = InputEvent::NONE;
  for (InputEvent event; (event = inputs.nextEvent()) != InputEvent::NONE;) {
    if (priorityOf(event) < priorityOf(selected)) selected = event;
  }
  if (selected != InputEvent::NONE) measurement.handleEvent(selected);
  measurement.update();

  networkManager.update();
  web.update();
  delay(1); // Yield Wi-Fi; movement itself runs from the hardware timer ISR.
}
