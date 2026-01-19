#include "mqtt_manager.hpp"
#include <gtest/gtest.h>

class MqttManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.broker_address = "tcp://localhost:1883";
        config_.client_id = "test_client";
        config_.username = "";
        config_.password = "";
        config_.qos = 1;
        config_.retained = true;
        config_.keep_alive_sec = 60;
        config_.operation_timeout_ms = 500;
    }
    
    MqttConfig config_;
};

TEST_F(MqttManagerTest, ConstructorDestructor) {
    ASSERT_NO_THROW({
        MqttManager manager(config_);
    });
}

TEST_F(MqttManagerTest, InitialState) {
    MqttManager manager(config_);
    
    EXPECT_FALSE(manager.is_connected());
}

TEST_F(MqttManagerTest, Statistics) {
    MqttManager manager(config_);
    
    auto stats = manager.get_stats();
    EXPECT_EQ(stats->publish_success, 0);
    EXPECT_EQ(stats->publish_errors, 0);
    EXPECT_EQ(stats->messages_received, 0);
}

TEST_F(MqttManagerTest, ResetStatistics) {
    MqttManager manager(config_);
    
    manager.reset_stats();
    
    auto stats = manager.get_stats();
    EXPECT_EQ(stats->publish_success, 0);
    EXPECT_EQ(stats->publish_errors, 0);
    EXPECT_EQ(stats->messages_received, 0);
}

TEST_F(MqttManagerTest, MessageCallback) {
    MqttManager manager(config_);
    
    bool callback_called = false;
    std::string received_topic;
    std::string received_payload;
    
    manager.set_message_callback([&](const std::string& topic, const std::string& payload) {
        callback_called = true;
        received_topic = topic;
        received_payload = payload;
    });
}
