#pragma once

#include "config.hpp"
#include "i_mqtt_manager.hpp"
#include "logger/logger.hpp"
#include <mqtt/async_client.h>
#include <atomic>
#include <mutex>

struct MqttManagerStats {
    int publish_success;
    int publish_errors;
    int messages_received;

    MqttManagerStats(int ps,int pe,int mr);
};

class MqttManager : public mqtt::callback, public IMqttManager {
public:
    explicit MqttManager(const MqttConfig& config);
    virtual ~MqttManager();
    
    // Prevent copying
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;
    
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    bool virtual subscribe(const std::string& topic);
    bool virtual publish(const std::string& topic, const std::string& payload, bool retained = true);
    
    void set_message_callback(MqttMessageCallback callback);
    
    std::unique_ptr<MqttManagerStats> get_stats() const;
    void reset_stats();
    
private:
    MqttConfig config_;
    std::unique_ptr<mqtt::async_client> client_;
    MqttMessageCallback message_callback_;
    mutable std::mutex mutex_;
    
    std::atomic<int> publish_success_;
    std::atomic<int> publish_errors_;
    std::atomic<int> messages_received_;
    
    Logger logger_; 

    // MQTT callback overrides
    void message_arrived(mqtt::const_message_ptr msg) override;
    void connection_lost(const std::string& cause) override;
    void connected(const std::string& cause) override;
};