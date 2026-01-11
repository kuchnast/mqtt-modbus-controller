#include "mqtt_manager.hpp"
#include <iostream>

MqttManager::MqttManager(const MqttConfig& config)
    : config_(config), publish_success_(0), publish_errors_(0), messages_received_(0) {
    
    client_ = std::make_unique<mqtt::async_client>(
        config_.broker_address,
        config_.client_id
    );
    
    client_->set_callback(*this);
}

MqttManager::~MqttManager() {
    disconnect();
}

bool MqttManager::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);
        connOpts.set_keep_alive_interval(config_.keep_alive_sec);
        
        if (!config_.username.empty()) {
            connOpts.set_user_name(config_.username);
            connOpts.set_password(config_.password);
        }
        
        mqtt::message willmsg("modbus/poller/status", "offline", config_.qos, config_.retained);
        mqtt::will_options will(willmsg);
        connOpts.set_will(will);
        
        std::cout << "Connecting to MQTT broker: " << config_.broker_address << "..." << std::endl;
        
        auto conntok = client_->connect(connOpts);
        if (!conntok->wait_for(std::chrono::milliseconds(5000))) {
            std::cerr << "MQTT connection timeout" << std::endl;
            return false;
        }
        
        std::cout << "MQTT connected" << std::endl;
        
        // Publish online status
        auto msg = mqtt::make_message("modbus/poller/status", "online");
        msg->set_qos(config_.qos);
        msg->set_retained(config_.retained);
        client_->publish(msg);
        
        return true;
        
    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT connection error: " << exc.what() << std::endl;
        return false;
    }
}

void MqttManager::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (client_ && client_->is_connected()) {
        try {
            auto msg = mqtt::make_message("modbus/poller/status", "offline");
            msg->set_qos(config_.qos);
            msg->set_retained(config_.retained);
            
            auto tok = client_->publish(msg);
            tok->wait_for(std::chrono::milliseconds(1000));
            
            client_->disconnect()->wait_for(std::chrono::milliseconds(1000));
            
            std::cout << "MQTT disconnected" << std::endl;
        } catch (const mqtt::exception& exc) {
            std::cerr << "MQTT disconnect error: " << exc.what() << std::endl;
        }
    }
}

bool MqttManager::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && client_->is_connected();
}

bool MqttManager::subscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto subtok = client_->subscribe(topic, config_.qos);
        if (subtok->wait_for(std::chrono::milliseconds(2000))) {
            std::cout << "Subscribed: " << topic << std::endl;
            return true;
        } else {
            std::cerr << "MQTT subscribe timeout: " << topic << std::endl;
            return false;
        }
    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT subscribe error: " << exc.what() << std::endl;
        return false;
    }
}

bool MqttManager::publish(const std::string& topic, const std::string& payload, bool retained) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(config_.qos);
        msg->set_retained(retained);
        
        auto tok = client_->publish(msg);
        
        if (tok->wait_for(std::chrono::milliseconds(config_.operation_timeout_ms))) {
            publish_success_++;
            return true;
        } else {
            publish_errors_++;
            std::cerr << "MQTT publish timeout: " << topic << std::endl;
            return false;
        }
        
    } catch (const mqtt::exception& exc) {
        publish_errors_++;
        std::cerr << "MQTT publish error (" << topic << "): " << exc.what() << std::endl;
        return false;
    }
}

void MqttManager::set_message_callback(MqttMessageCallback callback) {
    message_callback_ = callback;
}

void MqttManager::message_arrived(mqtt::const_message_ptr msg) {
    messages_received_++;
    
    if (message_callback_) {
        message_callback_(msg->get_topic(), msg->to_string());
    }
}

void MqttManager::connection_lost(const std::string& cause) {
    std::cerr << "MQTT connection lost: " << cause << std::endl;
    std::cerr << "Auto-reconnect should restore connection..." << std::endl;
}

void MqttManager::connected(const std::string& cause) {
    std::cout << "MQTT reconnected successfully" << std::endl;
}

MqttManager::Stats MqttManager::get_stats() const {
    return {
        publish_success_.load(),
        publish_errors_.load(),
        messages_received_.load()
    };
}

void MqttManager::reset_stats() {
    publish_success_ = 0;
    publish_errors_ = 0;
    messages_received_ = 0;
}