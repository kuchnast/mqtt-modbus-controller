#pragma once

#include "mqtt_manager.hpp"
#include <gmock/gmock.h>

// Mock implementation
class MockMqttManager : public IMqttManager {
public:
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, is_connected, (), (const, override));
    
    MOCK_METHOD(bool, subscribe, (const std::string& topic), (override));
    MOCK_METHOD(bool, publish, (const std::string& topic, const std::string& payload, bool retained), (override));
    
    MOCK_METHOD(void, set_message_callback, (MqttMessageCallback callback), (override));
    
    MOCK_METHOD(std::unique_ptr<MqttManagerStats>, get_stats, (), (const, override));
    MOCK_METHOD(void, reset_stats, (), (override));
    
    // Helper method to trigger message callback for testing
    void trigger_message(const std::string& topic, const std::string& payload) {
        if (callback_) {
            callback_(topic, payload);
        }
    }
    
    // Store callback for later use
    void set_message_callback_impl(MqttMessageCallback callback) {
        callback_ = callback;
    }
    
private:
    MqttMessageCallback callback_;
};

// Adapter to make MqttManager implement IMqttManager
class MqttManagerAdapter : public IMqttManager {
public:
    explicit MqttManagerAdapter(MqttManager& manager) : manager_(manager) {}
    
    bool connect() override { return manager_.connect(); }
    void disconnect() override { manager_.disconnect(); }
    bool is_connected() const override { return manager_.is_connected(); }
    
    bool subscribe(const std::string& topic) override {
        return manager_.subscribe(topic);
    }
    
    bool publish(const std::string& topic, const std::string& payload, bool retained = true) override {
        return manager_.publish(topic, payload, retained);
    }
    
    void set_message_callback(MqttMessageCallback callback) override {
        manager_.set_message_callback(callback);
    }
    
    std::unique_ptr<MqttManagerStats> get_stats() const override { return manager_.get_stats(); }
    void reset_stats() override { manager_.reset_stats(); }
    
private:
    MqttManager& manager_;
};