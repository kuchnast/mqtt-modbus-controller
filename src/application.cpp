#include "application.hpp"
#include <iostream>
#include <thread>

Application::Application(const std::string& config_file)
    : config_(std::make_unique<Config>(config_file)) {
}

Application::~Application() = default;

bool Application::initialize() {
    print_configuration();
    
    // Initialize Modbus
    modbus_ = std::make_unique<ModbusManager>(config_->modbus());
    if (!modbus_->connect()) {
        std::cerr << "Failed to initialize Modbus" << std::endl;
        return false;
    }
    
    // Initialize MQTT
    mqtt_ = std::make_unique<MqttManager>(config_->mqtt());
    if (!mqtt_->connect()) {
        std::cerr << "Failed to initialize MQTT" << std::endl;
        return false;
    }
    
    // Subscribe to relay commands
    mqtt_->subscribe("modbus/relay/+/set");
    
    // Initialize Device Controller
    controller_ = std::make_unique<DeviceController>(
        config_->inputs(),
        config_->relays(),
        config_->polling(),
        *modbus_,
        *mqtt_
    );
    
    // Set MQTT message callback
    mqtt_->set_message_callback([this](const std::string& topic, const std::string& payload) {
        controller_->handle_mqtt_command(topic, payload);
    });
    
    std::cout << "\n✓ Application initialized successfully\n" << std::endl;
    
    return true;
}

void Application::run(std::atomic<bool>& running, std::atomic<bool>& force_exit) {
    controller_->start_watchdog(running, force_exit);
    
    std::cout << "Starting main polling loop..." << std::endl;
    std::cout << "Poll interval: " << config_->polling().poll_interval_ms << "ms" << std::endl;
    std::cout << "Refresh interval: " << config_->polling().refresh_interval_sec << "s\n" << std::endl;
    
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
            end_time - start_time).count();
        
        int poll_interval = config_->polling().poll_interval_ms;
        if (elapsed < poll_interval) {
            int sleep_time = poll_interval - elapsed;
            
            // Sleep in small chunks to allow quick exit
            for (int i = 0; i < sleep_time / 10 && !force_exit; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    std::cout << "Main loop terminated" << std::endl;
}

void Application::shutdown() {
    std::cout << "\nShutting down application..." << std::endl;
    
    if (mqtt_) {
        mqtt_->disconnect();
    }
    
    if (modbus_) {
        modbus_->disconnect();
    }
    
    std::cout << "✓ Application shutdown complete" << std::endl;
}

void Application::print_configuration() const {
    std::cout << "\n===== CONFIGURATION =====" << std::endl;
    std::cout << "Modbus: " << config_->modbus().port 
              << " @ " << config_->modbus().baudrate << " baud" << std::endl;
    std::cout << "MQTT: " << config_->mqtt().broker_address << std::endl;
    std::cout << "Digital Inputs: " << config_->inputs().size() << std::endl;
    std::cout << "Relays: " << config_->relays().size() << std::endl;
    std::cout << "==========================\n" << std::endl;
}