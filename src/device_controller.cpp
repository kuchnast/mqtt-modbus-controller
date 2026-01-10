#include "device_controller.hpp"
#include <iostream>
#include <thread>

DeviceController::DeviceController(
    const std::vector<DigitalInput>& inputs,
    const std::vector<Relay>& relays,
    const PollingConfig& polling_config,
    ModbusManager& modbus,
    MqttManager& mqtt
) : polling_config_(polling_config), modbus_(modbus), mqtt_(mqtt),
    last_loop_time_(std::chrono::steady_clock::now()),
    last_stats_time_(std::chrono::steady_clock::now()) {
    
    // Initialize input states
    for (const auto& input : inputs) {
        input_states_.emplace_back(&input);
    }
    
    // Initialize relay states
    for (const auto& relay : relays) {
        relay_states_.emplace(relay.name, RelayState(&relay));
    }
}

void DeviceController::poll_inputs() {
    std::map<int, std::vector<InputState*>> inputs_by_slave;
    group_inputs_by_slave(inputs_by_slave);
    
    uint8_t input_bits[8];
    
    for (auto& [slave_id, slave_inputs] : inputs_by_slave) {
        // Read 8 inputs at once from this slave
        if (modbus_.read_discrete_inputs(slave_id, 0, 8, input_bits)) {
            for (auto* state : slave_inputs) {
                bool current_state = input_bits[state->input->address];
                publish_input_state(*state, current_state);
                state->last_state = current_state;
            }
        }
    }
}

void DeviceController::process_relay_commands() {
    std::vector<RelayCommand> commands;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        commands = relay_command_queue_;
        relay_command_queue_.clear();
    }
    
    // Limit commands per cycle to avoid flooding the bus
    if (commands.size() > static_cast<size_t>(polling_config_.max_commands_per_cycle)) {
        std::cerr << "WARNING: " << commands.size() 
                  << " commands in queue, limiting to " 
                  << polling_config_.max_commands_per_cycle << "/cycle" << std::endl;
        commands.resize(polling_config_.max_commands_per_cycle);
    }
    
    for (const auto& cmd : commands) {
        auto it = relay_states_.find(cmd.relay_name);
        if (it != relay_states_.end()) {
            RelayState& state = it->second;
            
            if (modbus_.write_coil(
                state.relay->slave_id,
                state.relay->address,
                cmd.desired_state)) {
                
                state.current_state = cmd.desired_state;
                publish_relay_state(state);
                
                std::cout << "RELAY: " << state.relay->name 
                          << " @ slave " << state.relay->slave_id
                          << " addr " << state.relay->address << " = "
                          << (cmd.desired_state ? "ON" : "OFF") << std::endl;
            } else {
                std::cerr << "ERROR: Failed to set relay " << cmd.relay_name << std::endl;
            }
        }
    }
}

void DeviceController::handle_mqtt_command(const std::string& topic, const std::string& payload) {
    // Parse relay command: "modbus/relay/{name}/set"
    const std::string prefix = "modbus/relay/";
    const std::string suffix = "/set";
    
    if (topic.find(prefix) == 0 && topic.find(suffix) != std::string::npos) {
        size_t start = prefix.length();
        size_t end = topic.find(suffix);
        std::string relay_name = topic.substr(start, end - start);
        
        bool state = (payload == "ON" || payload == "1" || payload == "true");
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            relay_command_queue_.push_back({relay_name, state});
        }
        
        std::cout << "MQTT CMD: " << relay_name << " = " << payload << std::endl;
    }
}

void DeviceController::print_statistics() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_stats_time_).count();
    
    if (elapsed < 60) {
        return;
    }
    
    auto modbus_stats = modbus_.get_stats();
    auto mqtt_stats = mqtt_.get_stats();
    
    int total_reads = modbus_stats.read_success + modbus_stats.read_errors;
    int total_writes = modbus_stats.write_success + modbus_stats.write_errors;
    int total_mqtt = mqtt_stats.publish_success + mqtt_stats.publish_errors;
    
    std::cout << "\n===== STATISTICS (60s) =====" << std::endl;
    
    std::cout << "Modbus Reads: " << modbus_stats.read_success << "/" << total_reads;
    if (total_reads > 0) {
        std::cout << " (" << (100.0 * modbus_stats.read_success / total_reads) << "%)";
    }
    std::cout << std::endl;
    
    std::cout << "Modbus Writes: " << modbus_stats.write_success << "/" << total_writes;
    if (total_writes > 0) {
        std::cout << " (" << (100.0 * modbus_stats.write_success / total_writes) << "%)";
    }
    std::cout << std::endl;
    
    std::cout << "MQTT Publishes: " << mqtt_stats.publish_success << "/" << total_mqtt;
    if (total_mqtt > 0) {
        std::cout << " (" << (100.0 * mqtt_stats.publish_success / total_mqtt) << "%)";
    }
    std::cout << std::endl;
    
    std::cout << "MQTT Messages Received: " << mqtt_stats.messages_received << std::endl;
    std::cout << std::endl;
    
    modbus_.reset_stats();
    mqtt_.reset_stats();
    last_stats_time_ = now;
}

void DeviceController::start_watchdog(std::atomic<bool>& running, std::atomic<bool>& force_exit) {
    std::thread watchdog([this, &running, &force_exit]() {
        std::cout << "Watchdog: started (alarm after " 
                  << polling_config_.watchdog_timeout_sec << "s)" << std::endl;
        
        while (running && !force_exit) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            auto now = std::chrono::steady_clock::now();
            auto last = last_loop_time_.load();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last).count();
            
            if (elapsed > polling_config_.watchdog_timeout_sec && running) {
                std::cerr << "\n!!! WATCHDOG ALARM !!!" << std::endl;
                std::cerr << "Main loop not responding for " << elapsed << "s!" << std::endl;
                std::cerr << "Forcing restart..." << std::endl;
                force_exit = true;
                raise(SIGTERM);
            }
        }
    });
    
    watchdog.detach();
}

void DeviceController::update_watchdog() {
    last_loop_time_ = std::chrono::steady_clock::now();
}

void DeviceController::publish_input_state(InputState& state, bool current_state, bool force) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.last_publish).count();
    
    bool should_publish = force || 
                         (current_state != state.last_state) ||
                         (elapsed >= polling_config_.refresh_interval_sec);
    
    if (should_publish) {
        const char* payload = current_state ? "ON" : "OFF";
        
        if (mqtt_.publish(state.input->mqtt_topic, payload, true)) {
            if (current_state != state.last_state) {
                std::cout << "INPUT: " << state.input->name << " = " << payload << std::endl;
            }
        }
        
        state.last_publish = now;
    }
}

void DeviceController::publish_relay_state(const RelayState& state) {
    const char* payload = state.current_state ? "ON" : "OFF";
    mqtt_.publish(state.relay->mqtt_state_topic, payload, true);
}

void DeviceController::group_inputs_by_slave(std::map<int, std::vector<InputState*>>& groups) {
    for (auto& state : input_states_) {
        groups[state.input->slave_id].push_back(&state);
    }
}