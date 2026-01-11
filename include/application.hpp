#pragma once

#include "config.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include "device_controller.hpp"
#include "logger/logger.hpp"
#include <memory>
#include <atomic>

class Application {
public:
    explicit Application(const std::string& config_file);
    ~Application();
    
    bool initialize();
    void run(std::atomic<bool>& running, std::atomic<bool>& force_exit);
    void shutdown();
    
private:
    std::unique_ptr<Config> config_;
    std::unique_ptr<ModbusManager> modbus_;
    std::unique_ptr<MqttManager> mqtt_;
    std::unique_ptr<DeviceController> controller_;
    
    Logger logger_;
};