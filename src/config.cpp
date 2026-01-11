#include "config.hpp"
#include <fstream>
#include <stdexcept>

ModbusConfig ModbusConfig::from_json(const nlohmann::json& j) {
    ModbusConfig config;
    config.port = j.value("port", "/dev/ttyUSB0");
    config.baudrate = j.value("baudrate", 9600);
    
    std::string parity = j.value("parity", "N");
    config.parity = parity.empty() ? 'N' : parity[0];
    
    config.data_bits = j.value("data_bits", 8);
    config.stop_bits = j.value("stop_bits", 1);
    config.response_timeout_ms = j.value("response_timeout_ms", 300);
    config.byte_timeout_ms = j.value("byte_timeout_ms", 100);
    config.max_retries = j.value("max_retries", 3);
    
    return config;
}

MqttConfig MqttConfig::from_json(const nlohmann::json& j) {
    MqttConfig config;
    config.broker_address = j.value("broker_address", "tcp://localhost:1883");
    config.client_id = j.value("client_id", "modbus_poller");
    config.username = j.value("username", "");
    config.password = j.value("password", "");
    config.qos = j.value("qos", 1);
    config.retained = j.value("retained", true);
    config.keep_alive_sec = j.value("keep_alive_sec", 60);
    config.operation_timeout_ms = j.value("operation_timeout_ms", 500);
    
    return config;
}

DigitalInput DigitalInput::from_json(const nlohmann::json& j) {
    DigitalInput input;
    input.slave_id = j.at("slave_id").get<int>();
    input.address = j.at("address").get<int>();
    input.name = j.at("name").get<std::string>();
    
    if (j.contains("mqtt_topic")) {
        input.mqtt_topic = j.at("mqtt_topic").get<std::string>();
    } else {
        input.mqtt_topic = "modbus/input/" + input.name + "/state";
    }
    
    return input;
}

Relay Relay::from_json(const nlohmann::json& j) {
    Relay relay;
    relay.slave_id = j.at("slave_id").get<int>();
    relay.address = j.at("address").get<int>();
    relay.name = j.at("name").get<std::string>();
    
    if (j.contains("mqtt_command_topic")) {
        relay.mqtt_command_topic = j.at("mqtt_command_topic").get<std::string>();
    } else {
        relay.mqtt_command_topic = "modbus/relay/" + relay.name + "/set";
    }
    
    if (j.contains("mqtt_state_topic")) {
        relay.mqtt_state_topic = j.at("mqtt_state_topic").get<std::string>();
    } else {
        relay.mqtt_state_topic = "modbus/relay/" + relay.name + "/state";
    }
    
    return relay;
}

PollingConfig PollingConfig::from_json(const nlohmann::json& j) {
    PollingConfig config;
    config.poll_interval_ms = j.value("poll_interval_ms", 400);
    config.refresh_interval_sec = j.value("refresh_interval_sec", 10);
    config.max_commands_per_cycle = j.value("max_commands_per_cycle", 10);
    config.watchdog_timeout_sec = j.value("watchdog_timeout_sec", 10);
    
    return config;
}

Config::Config(const std::string& filename) {
    load(filename);
}

void Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filename);
    }
    
    nlohmann::json j;
    file >> j;
    
    modbus_ = ModbusConfig::from_json(j.at("modbus"));
    mqtt_ = MqttConfig::from_json(j.at("mqtt"));
    polling_ = PollingConfig::from_json(j.at("polling"));
    
    for (const auto& item : j.at("digital_inputs")) {
        inputs_.push_back(DigitalInput::from_json(item));
    }
    
    for (const auto& item : j.at("relays")) {
        relays_.push_back(Relay::from_json(item));
    }
}

void Config::save(const std::string& filename) const {
    nlohmann::json j;
    
    // Modbus config
    j["modbus"] = {
        {"port", modbus_.port},
        {"baudrate", modbus_.baudrate},
        {"parity", std::string(1, modbus_.parity)},
        {"data_bits", modbus_.data_bits},
        {"stop_bits", modbus_.stop_bits},
        {"response_timeout_ms", modbus_.response_timeout_ms},
        {"byte_timeout_ms", modbus_.byte_timeout_ms},
        {"max_retries", modbus_.max_retries}
    };
    
    // MQTT config
    j["mqtt"] = {
        {"broker_address", mqtt_.broker_address},
        {"client_id", mqtt_.client_id},
        {"username", mqtt_.username},
        {"password", mqtt_.password},
        {"qos", mqtt_.qos},
        {"retained", mqtt_.retained},
        {"keep_alive_sec", mqtt_.keep_alive_sec},
        {"operation_timeout_ms", mqtt_.operation_timeout_ms}
    };
    
    // Polling config
    j["polling"] = {
        {"poll_interval_ms", polling_.poll_interval_ms},
        {"refresh_interval_sec", polling_.refresh_interval_sec},
        {"max_commands_per_cycle", polling_.max_commands_per_cycle},
        {"watchdog_timeout_sec", polling_.watchdog_timeout_sec}
    };
    
    // Digital inputs
    j["digital_inputs"] = nlohmann::json::array();
    for (const auto& input : inputs_) {
        j["digital_inputs"].push_back({
            {"slave_id", input.slave_id},
            {"address", input.address},
            {"name", input.name},
            {"mqtt_topic", input.mqtt_topic}
        });
    }
    
    // Relays
    j["relays"] = nlohmann::json::array();
    for (const auto& relay : relays_) {
        j["relays"].push_back({
            {"slave_id", relay.slave_id},
            {"address", relay.address},
            {"name", relay.name},
            {"mqtt_command_topic", relay.mqtt_command_topic},
            {"mqtt_state_topic", relay.mqtt_state_topic}
        });
    }
    
    std::ofstream file(filename);
    file << j.dump(DEFAULT_INDENT);
}