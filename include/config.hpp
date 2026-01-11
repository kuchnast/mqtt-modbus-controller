#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#define DEFAULT_INDENT 2

struct ModbusConfig {
    std::string port;
    int baudrate;
    char parity;
    int data_bits;
    int stop_bits;
    int response_timeout_ms;
    int byte_timeout_ms;
    int max_retries;
    
    static ModbusConfig from_json(const nlohmann::json& j);
};

struct MqttConfig {
    std::string broker_address;
    std::string client_id;
    std::string username;
    std::string password;
    int qos;
    bool retained;
    int keep_alive_sec;
    int operation_timeout_ms;
    
    static MqttConfig from_json(const nlohmann::json& j);
};

struct DigitalInput {
    int slave_id;
    int address;
    std::string name;
    std::string mqtt_topic;
    
    static DigitalInput from_json(const nlohmann::json& j);
};

struct Relay {
    int slave_id;
    int address;
    std::string name;
    std::string mqtt_command_topic;
    std::string mqtt_state_topic;
    
    static Relay from_json(const nlohmann::json& j);
};

struct PollingConfig {
    int poll_interval_ms;
    int refresh_interval_sec;
    int max_commands_per_cycle;
    int watchdog_timeout_sec;
    
    static PollingConfig from_json(const nlohmann::json& j);
};

class Config {
public:
    explicit Config(const std::string& filename);
    
    const ModbusConfig& modbus() const { return modbus_; }
    const MqttConfig& mqtt() const { return mqtt_; }
    const PollingConfig& polling() const { return polling_; }
    const std::vector<DigitalInput>& inputs() const { return inputs_; }
    const std::vector<Relay>& relays() const { return relays_; }
    
    void save(const std::string& filename) const;
    
private:
    ModbusConfig modbus_;
    MqttConfig mqtt_;
    PollingConfig polling_;
    std::vector<DigitalInput> inputs_;
    std::vector<Relay> relays_;
    
    void load(const std::string& filename);
};