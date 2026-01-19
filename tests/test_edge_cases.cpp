#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device_controller.hpp"
#include "modbus_manager_mock.hpp"
#include "mqtt_manager_mock.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArrayArgument;
using ::testing::Invoke;

class EdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        polling_config_.poll_interval_ms = 100;
        polling_config_.refresh_interval_sec = 5;
        polling_config_.max_commands_per_cycle = 10;
        polling_config_.watchdog_timeout_sec = 10;
        
        mock_modbus_ = std::make_unique<MockModbusManager>();
        mock_mqtt_ = std::make_unique<MockMqttManager>();
    }
    
    PollingConfig polling_config_;
    std::unique_ptr<MockModbusManager> mock_modbus_;
    std::unique_ptr<MockMqttManager> mock_mqtt_;
};

TEST_F(EdgeCaseTest, EmptyConfiguration) {
    std::vector<DigitalInput> empty_inputs;
    std::vector<Relay> empty_relays;
    
    // Should not crash with empty config
    EXPECT_NO_THROW({
        DeviceController controller(empty_inputs, empty_relays, polling_config_, 
                                   *mock_modbus_, *mock_mqtt_);
        controller.poll_inputs();
        controller.process_relay_commands();
    });
}

TEST_F(EdgeCaseTest, SingleInputSingleRelay) {
    std::vector<DigitalInput> inputs;
    DigitalInput input;
    input.slave_id = 1;
    input.address = 0;
    input.name = "single_input";
    input.mqtt_topic = "test/input";
    inputs.push_back(input);
    
    std::vector<Relay> relays;
    Relay relay;
    relay.slave_id = 1;
    relay.address = 0;
    relay.name = "single_relay";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    std::array<uint8_t, 8> state = {1, 0, 0, 0, 0, 0, 0, 0};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
        .WillOnce([state](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state;
                return true;
            }
        );
    EXPECT_CALL(*mock_mqtt_, publish("test/input", "ON", true))
        .WillOnce(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.poll_inputs();
}

TEST_F(EdgeCaseTest, MaxInputsPerSlave) {
    // Test with 8 inputs (maximum per read operation)
    std::vector<DigitalInput> inputs;
    for (int i = 0; i < 8; i++) {
        DigitalInput input;
        input.slave_id = 1;
        input.address = i;
        input.name = "input" + std::to_string(i);
        input.mqtt_topic = "test/input" + std::to_string(i);
        inputs.push_back(input);
    }
    
    std::vector<Relay> relays;
    std::array<uint8_t, 8> state = {1, 1, 1, 1, 1, 1, 1, 1};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
        .WillOnce(Invoke(
            [state](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state;
                return true;
            }
        ));
    
    // Should publish 8 times
    EXPECT_CALL(*mock_mqtt_, publish(_, "ON", true))
        .Times(8)
        .WillRepeatedly(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.poll_inputs();
}

TEST_F(EdgeCaseTest, VeryLongTopicNames) {
    std::vector<DigitalInput> inputs;
    DigitalInput input;
    input.slave_id = 1;
    input.address = 0;
    input.name = "input";
    input.mqtt_topic = std::string(256, 'a') + "/very/long/topic/name/test";
    inputs.push_back(input);
    
    std::vector<Relay> relays;
    std::array<uint8_t, 8> state = {1, 0, 0, 0, 0, 0, 0, 0};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
        .WillOnce([state](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state;
                return true;
            }
        );
    EXPECT_CALL(*mock_mqtt_, publish(input.mqtt_topic, "ON", true))
        .WillOnce(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.poll_inputs();
}

TEST_F(EdgeCaseTest, SpecialCharactersInNames) {
    std::vector<DigitalInput> inputs;
    std::vector<Relay> relays;
    
    Relay relay;
    relay.slave_id = 1;
    relay.address = 0;
    relay.name = "relay-with/special@chars#$%";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    
    // Should handle special characters without crashing
    EXPECT_NO_THROW({
        controller.handle_mqtt_command("test/relay/set", "ON");
    });
}

TEST_F(EdgeCaseTest, RapidStateChanges) {
    std::vector<DigitalInput> inputs;
    DigitalInput input;
    input.slave_id = 1;
    input.address = 0;
    input.name = "input";
    input.mqtt_topic = "test/input";
    inputs.push_back(input);
    
    std::vector<Relay> relays;
    
    // Simulate rapid state changes: OFF -> ON -> OFF -> ON
    std::array<uint8_t, 8> state_off = {0, 0, 0, 0, 0, 0, 0, 0};
    std::array<uint8_t, 8> state_on = {1, 0, 0, 0, 0, 0, 0, 0};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
        .WillOnce([state_off](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state_off;
                return true;
            })
        .WillOnce([state_on](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state_on;
                return true;
            })
        .WillOnce([state_off](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state_off;
                return true;
            })
        .WillOnce(Invoke(
            [state_on](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state_on;
                return true;
            }
        ));
    
    EXPECT_CALL(*mock_mqtt_, publish("test/input", _, true))
        .Times(3) // Should publish only on state changes
        .WillRepeatedly(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    
    for (int i = 0; i < 4; i++) {
        controller.poll_inputs();
    }
}

TEST_F(EdgeCaseTest, ZeroSlaveId) {
    // Slave ID 0 is broadcast address, should still work
    std::vector<DigitalInput> inputs;
    std::vector<Relay> relays;
    
    Relay relay;
    relay.slave_id = 0;
    relay.address = 0;
    relay.name = "broadcast_relay";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.handle_mqtt_command("modbus/relay/broadcast_relay/set", "ON");
    
    EXPECT_CALL(*mock_modbus_, write_coil(0, 0, true))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_mqtt_, publish("test/relay/state", "ON", true))
        .WillOnce(Return(true));
    
    controller.process_relay_commands();
}

TEST_F(EdgeCaseTest, MaxSlaveId) {
    // Max Modbus slave ID is 247
    std::vector<DigitalInput> inputs;
    DigitalInput input;
    input.slave_id = 247;
    input.address = 0;
    input.name = "max_slave_input";
    input.mqtt_topic = "test/input";
    inputs.push_back(input);
    
    std::vector<Relay> relays;
    std::array<uint8_t, 8> state = {1, 0, 0, 0, 0, 0, 0, 0};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(247, 0, _))
        .WillOnce([state](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state;
                return true;
            });
    EXPECT_CALL(*mock_mqtt_, publish("test/input", "ON", true))
        .WillOnce(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.poll_inputs();
}

TEST_F(EdgeCaseTest, MaxAddress) {
    // Max address for discrete inputs is typically 9999 or 65535
    std::vector<DigitalInput> inputs;
    DigitalInput input;
    input.slave_id = 1;
    input.address = 7;  // Last valid address in 8-bit array
    input.name = "max_addr_input";
    input.mqtt_topic = "test/input";
    inputs.push_back(input);
    
    std::vector<Relay> relays;
    std::array<uint8_t, 8> state = {0, 0, 0, 0, 0, 0, 0, 1};
    
    EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
        .WillOnce(Invoke(
            [state](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
                dest = state;
                return true;
            }
        ));
    EXPECT_CALL(*mock_mqtt_, publish("test/input", "ON", true))
        .WillOnce(Return(true));
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    controller.poll_inputs();
}

TEST_F(EdgeCaseTest, EmptyPayload) {
    std::vector<DigitalInput> inputs;
    std::vector<Relay> relays;
    
    Relay relay;
    relay.slave_id = 1;
    relay.address = 0;
    relay.name = "relay1";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    
    // Empty payload should be treated as OFF
    controller.handle_mqtt_command("modbus/relay/relay1/set", "");
    
    EXPECT_CALL(*mock_modbus_, write_coil(1, 0, false))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_mqtt_, publish("test/relay/state", "OFF", true))
        .WillOnce(Return(true));
    
    controller.process_relay_commands();
}

TEST_F(EdgeCaseTest, VeryLongPayload) {
    std::vector<DigitalInput> inputs;
    std::vector<Relay> relays;
    
    Relay relay;
    relay.slave_id = 1;
    relay.address = 0;
    relay.name = "relay1";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    
    // Very long payload - should handle gracefully
    std::string long_payload(10000, 'x');
    
    EXPECT_NO_THROW({
        controller.handle_mqtt_command("modbus/relay/relay1/set", long_payload);
    });
}

TEST_F(EdgeCaseTest, CommandQueueOverflow) {
    std::vector<DigitalInput> inputs;
    std::vector<Relay> relays;
    
    Relay relay;
    relay.slave_id = 1;
    relay.address = 0;
    relay.name = "relay1";
    relay.mqtt_command_topic = "test/relay/set";
    relay.mqtt_state_topic = "test/relay/state";
    relays.push_back(relay);
    
    polling_config_.max_commands_per_cycle = 1;
    
    DeviceController controller(inputs, relays, polling_config_, *mock_modbus_, *mock_mqtt_);
    
    // Queue 100 commands
    for (int i = 0; i < 100; i++) {
        controller.handle_mqtt_command("modbus/relay/relay1/set", "ON");
    }
    
    // Should process only 1 per cycle
    EXPECT_CALL(*mock_modbus_, write_coil(1, 0, true))
        .Times(1)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_mqtt_, publish("test/relay/state", "ON", true))
        .Times(1)
        .WillRepeatedly(Return(true));
    
    controller.process_relay_commands();
}