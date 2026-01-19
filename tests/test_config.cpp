#include "config.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

class ConfigTest : public ::testing::Test {
protected:
  void SetUp() override { test_config_file_ = "test_config.json"; }

  void TearDown() override {
    if (std::filesystem::exists(test_config_file_)) {
      std::filesystem::remove(test_config_file_);
    }
  }

  void create_valid_config() {
    std::ofstream file(test_config_file_);
    file << R"({
            "modbus": {
                "port": "/dev/ttyUSB0",
                "baudrate": 9600,
                "parity": "N",
                "data_bits": 8,
                "stop_bits": 1,
                "response_timeout_ms": 300,
                "byte_timeout_ms": 100,
                "max_retries": 3
            },
            "mqtt": {
                "broker_address": "tcp://localhost:1883",
                "client_id": "test_client",
                "username": "user",
                "password": "pass",
                "qos": 1,
                "retained": true,
                "keep_alive_sec": 60,
                "operation_timeout_ms": 500
            },
            "polling": {
                "poll_interval_ms": 400,
                "refresh_interval_sec": 10,
                "max_commands_per_cycle": 10,
                "watchdog_timeout_sec": 10
            },
            "digital_inputs": [
                {
                    "slave_id": 1,
                    "address": 0,
                    "name": "input1",
                    "mqtt_topic": "test/input1"
                }
            ],
            "relays": [
                {
                    "slave_id": 1,
                    "address": 0,
                    "name": "relay1",
                    "mqtt_command_topic": "test/relay1/set",
                    "mqtt_state_topic": "test/relay1/state"
                }
            ]
        })";
    file.close();
  }

  std::string test_config_file_;
};

TEST_F(ConfigTest, LoadValidConfig) {
  create_valid_config();

  ASSERT_NO_THROW({
    Config config(test_config_file_);

    // Check Modbus config
    EXPECT_EQ(config.modbus().port, "/dev/ttyUSB0");
    EXPECT_EQ(config.modbus().baudrate, 9600);
    EXPECT_EQ(config.modbus().parity, 'N');
    EXPECT_EQ(config.modbus().data_bits, 8);
    EXPECT_EQ(config.modbus().stop_bits, 1);
    EXPECT_EQ(config.modbus().response_timeout_ms, 300);
    EXPECT_EQ(config.modbus().byte_timeout_ms, 100);
    EXPECT_EQ(config.modbus().max_retries, 3);

    // Check MQTT config
    EXPECT_EQ(config.mqtt().broker_address, "tcp://localhost:1883");
    EXPECT_EQ(config.mqtt().client_id, "test_client");
    EXPECT_EQ(config.mqtt().username, "user");
    EXPECT_EQ(config.mqtt().password, "pass");
    EXPECT_EQ(config.mqtt().qos, 1);
    EXPECT_TRUE(config.mqtt().retained);
    EXPECT_EQ(config.mqtt().keep_alive_sec, 60);
    EXPECT_EQ(config.mqtt().operation_timeout_ms, 500);

    // Check Polling config
    EXPECT_EQ(config.polling().poll_interval_ms, 400);
    EXPECT_EQ(config.polling().refresh_interval_sec, 10);
    EXPECT_EQ(config.polling().max_commands_per_cycle, 10);
    EXPECT_EQ(config.polling().watchdog_timeout_sec, 10);

    // Check inputs
    EXPECT_EQ(config.inputs().size(), 1);
    EXPECT_EQ(config.inputs()[0].slave_id, 1);
    EXPECT_EQ(config.inputs()[0].address, 0);
    EXPECT_EQ(config.inputs()[0].name, "input1");
    EXPECT_EQ(config.inputs()[0].mqtt_topic, "test/input1");

    // Check relays
    EXPECT_EQ(config.relays().size(), 1);
    EXPECT_EQ(config.relays()[0].slave_id, 1);
    EXPECT_EQ(config.relays()[0].address, 0);
    EXPECT_EQ(config.relays()[0].name, "relay1");
    EXPECT_EQ(config.relays()[0].mqtt_command_topic, "test/relay1/set");
    EXPECT_EQ(config.relays()[0].mqtt_state_topic, "test/relay1/state");
  });
}

TEST_F(ConfigTest, LoadNonExistentFile) {
  EXPECT_THROW(
      { Config config("non_existent_file.json"); }, std::runtime_error);
}

TEST_F(ConfigTest, DefaultValues) {
  std::ofstream file(test_config_file_);
  file << R"({
        "modbus": {},
        "mqtt": {},
        "polling": {},
        "digital_inputs": [],
        "relays": []
    })";
  file.close();

  Config config(test_config_file_);

  // Check defaults
  EXPECT_EQ(config.modbus().port, "/dev/ttyUSB0");
  EXPECT_EQ(config.modbus().baudrate, 9600);
  EXPECT_EQ(config.modbus().parity, 'N');

  EXPECT_EQ(config.mqtt().broker_address, "tcp://localhost:1883");
  EXPECT_EQ(config.mqtt().client_id, "modbus_poller");

  EXPECT_EQ(config.polling().poll_interval_ms, 400);
  EXPECT_EQ(config.polling().refresh_interval_sec, 10);
}

TEST_F(ConfigTest, SaveConfig) {
  create_valid_config();
  Config config(test_config_file_);

  std::string save_file = "test_save_config.json";
  config.save(save_file);

  EXPECT_TRUE(std::filesystem::exists(save_file));

  // Load saved config and verify
  Config loaded_config(save_file);
  EXPECT_EQ(loaded_config.modbus().port, config.modbus().port);
  EXPECT_EQ(loaded_config.mqtt().broker_address, config.mqtt().broker_address);
  EXPECT_EQ(loaded_config.inputs().size(), config.inputs().size());
  EXPECT_EQ(loaded_config.relays().size(), config.relays().size());

  std::filesystem::remove(save_file);
}

TEST_F(ConfigTest, AutoGenerateMqttTopics) {
  std::ofstream file(test_config_file_);
  file << R"({
        "modbus": {},
        "mqtt": {},
        "polling": {},
        "digital_inputs": [
            {
                "slave_id": 1,
                "address": 0,
                "name": "sensor1"
            }
        ],
        "relays": [
            {
                "slave_id": 1,
                "address": 0,
                "name": "light1"
            }
        ]
    })";
  file.close();

  Config config(test_config_file_);

  // Check auto-generated topics
  EXPECT_EQ(config.inputs()[0].mqtt_topic, "modbus/input/sensor1/state");
  EXPECT_EQ(config.relays()[0].mqtt_command_topic, "modbus/relay/light1/set");
  EXPECT_EQ(config.relays()[0].mqtt_state_topic, "modbus/relay/light1/state");
}
