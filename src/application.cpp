#include "application.hpp"
#include <iostream>
#include <thread>

Application::Application(const std::string &config_file)
    : config_(std::make_unique<Config>(config_file)), logger_("Application") {}

Application::~Application() = default;

bool Application::initialize() {
  logger_.info() << "Modbus: " << config_->modbus().port << " @ "
                 << config_->modbus().baudrate << " baud";
  logger_.info() << "MQTT: " << config_->mqtt().broker_address;
  logger_.info() << "Digital Inputs: " << config_->inputs().size();
  logger_.info() << "Relays: " << config_->relays().size();

  // Initialize Modbus
  modbus_ = std::make_unique<ModbusManager>(config_->modbus());
  if (!modbus_->connect()) {
    logger_.critical() << "Failed to initialize Modbus";
    return false;
  }

  // Initialize MQTT
  mqtt_ = std::make_unique<MqttManager>(config_->mqtt());
  if (!mqtt_->connect()) {
    logger_.critical() << "Failed to initialize MQTT";
    return false;
  }

  // Subscribe to relay commands
  // TODO: Make mqtt subscriptions bundled with device types in config
  mqtt_->subscribe("modbus/relay/+/set");

  // Initialize Device Controller
  // TODO: Make device controller initialization bundled with device types in
  // config
  controller_ =
      std::make_unique<DeviceController>(config_->inputs(), config_->relays(),
                                         config_->polling(), *modbus_, *mqtt_);

  // Set MQTT message callback
  mqtt_->set_message_callback(
      [this](const std::string &topic, const std::string &payload) {
        controller_->handle_mqtt_command(topic, payload);
      });

  logger_.info() << "Application initialized successfully";

  return true;
}

void Application::run(std::atomic<bool> &running,
                      std::atomic<bool> &force_exit) {
  controller_->start_watchdog(running, force_exit);

  logger_.info() << "Starting main polling loop...";
  logger_.info() << "Poll interval: " << config_->polling().poll_interval_ms
                 << "ms";
  logger_.info() << "Refresh interval: "
                 << config_->polling().refresh_interval_sec << "s";

  while (running && !force_exit) {
    auto start_time = std::chrono::steady_clock::now();

    controller_->update_watchdog();

    // Poll all inputs
    controller_->poll_inputs();

    // Process relay commands
    controller_->process_relay_commands();

    // Print statistics
    controller_->print_statistics();

    // Sleep for remaining time
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       end_time - start_time)
                       .count();

    int poll_interval = config_->polling().poll_interval_ms;
    if (elapsed < poll_interval)
      std::this_thread::sleep_for(
          std::chrono::milliseconds(poll_interval - elapsed));
  }

  logger_.info() << "Main loop terminated";
}

void Application::shutdown() {
  logger_.info() << "Shutting down application...";

  if (mqtt_) {
    mqtt_->disconnect();
  }

  if (modbus_) {
    modbus_->disconnect();
  }

  logger_.info() << "Application shutdown complete";
}