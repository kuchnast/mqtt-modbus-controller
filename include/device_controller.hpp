#pragma once

#include "config.hpp"
#include "logger/logger.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include <map>
#include <chrono>
#include <atomic>

class DeviceController {
public:
    DeviceController(
        const std::vector<DigitalInput>& inputs,
        const std::vector<Relay>& relays,
        const PollingConfig& polling_config,
        IModbusManager& modbus,
        IMqttManager& mqtt
    );
    
    void poll_inputs();
    void process_relay_commands();
    void handle_mqtt_command(const std::string& topic, const std::string& payload);
    void print_statistics();
    
    void start_watchdog(std::atomic<bool>& running, std::atomic<bool>& force_exit);
    void update_watchdog();
    
private:
    struct InputState {
        const DigitalInput* input;
        bool last_state;
        std::chrono::steady_clock::time_point last_publish;
        
        InputState(const DigitalInput* inp) 
            : input(inp), last_state(false), 
              last_publish(std::chrono::steady_clock::now()) {}
    };
    
    struct RelayState {
        const Relay* relay;
        bool current_state;
        
        RelayState(const Relay* rel) 
            : relay(rel), current_state(false) {}
    };
    
    struct RelayCommand {
        std::string relay_name;
        bool desired_state;
    };
    
    std::vector<InputState> input_states_;
    std::map<std::string, RelayState> relay_states_;
    
    std::vector<RelayCommand> relay_command_queue_;
    std::mutex queue_mutex_;
    
    PollingConfig polling_config_;
    IModbusManager& modbus_;
    IMqttManager& mqtt_;
    
    std::atomic<std::chrono::steady_clock::time_point> last_loop_time_;
    std::chrono::steady_clock::time_point last_stats_time_;

    Logger logger_;
    
    void publish_input_state(InputState& state, bool current_state, bool force = false);
    void publish_relay_state(const RelayState& state);
    void group_inputs_by_slave(std::map<int, std::vector<InputState*>>& groups);
};