#include "device_controller.hpp"
#include "modbus_manager_mock.hpp"
#include "mqtt_manager_mock.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;

class DeviceControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup test data
    DigitalInput input1;
    input1.slave_id = 1;
    input1.address = 0;
    input1.name = "input1";
    input1.mqtt_topic = "test/input1/state";
    inputs_.push_back(input1);

    DigitalInput input2;
    input2.slave_id = 1;
    input2.address = 1;
    input2.name = "input2";
    input2.mqtt_topic = "test/input2/state";
    inputs_.push_back(input2);

    Relay relay1;
    relay1.slave_id = 1;
    relay1.address = 0;
    relay1.name = "relay1";
    relay1.mqtt_command_topic = "test/relay1/set";
    relay1.mqtt_state_topic = "test/relay1/state";
    relays_.push_back(relay1);

    polling_config_.poll_interval_ms = 100;
    polling_config_.refresh_interval_sec = 5;
    polling_config_.max_commands_per_cycle = 10;
    polling_config_.watchdog_timeout_sec = 10;

    mock_modbus_ = std::make_unique<MockModbusManager>();
    mock_mqtt_ = std::make_unique<MockMqttManager>();
  }

  std::vector<DigitalInput> inputs_;
  std::vector<Relay> relays_;
  PollingConfig polling_config_;
  std::unique_ptr<MockModbusManager> mock_modbus_;
  std::unique_ptr<MockMqttManager> mock_mqtt_;
};

TEST_F(DeviceControllerTest, PollInputsSuccess) {
  // Setup: Modbus returns successful read with specific input states
  std::array<uint8_t, 8> input_states = {1, 1, 0, 0, 0, 0, 0, 0};

  EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _))
      .WillOnce([input_states](int /*slave_id*/, int /*start_addr*/, std::array<uint8_t, 8>& dest) {
        dest = input_states;
        return true;
      });

  // Expect MQTT publishes for each input
  EXPECT_CALL(*mock_mqtt_, publish("test/input1/state", "ON", true)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/input2/state", "ON", true)).WillOnce(Return(true));

  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);
  controller.poll_inputs();
}

TEST_F(DeviceControllerTest, PollInputsModbusFailure) {
  // Setup: Modbus read fails
  EXPECT_CALL(*mock_modbus_, read_discrete_inputs(1, 0, _)).WillOnce(Return(false));

  // Should not publish anything on failure
  EXPECT_CALL(*mock_mqtt_, publish(_, _, _)).Times(0);

  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);
  controller.poll_inputs();
}

TEST_F(DeviceControllerTest, HandleRelayCommandON) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);
  controller.handle_mqtt_command("modbus/relay/relay1/set", "ON");

  // Now process the command
  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, true)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", "ON", true)).WillOnce(Return(true));

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, HandleRelayCommandOFF) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);
  controller.handle_mqtt_command("modbus/relay/relay1/set", "OFF");

  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, false)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", "OFF", true)).WillOnce(Return(true));

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, HandleRelayCommandWithRetry) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);
  controller.handle_mqtt_command("modbus/relay/relay1/set", "ON");

  // First attempt fails, should log error but not crash
  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, true)).WillOnce(Return(false));

  // Should not publish state on failure
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", _, _)).Times(0);

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, MultipleCommandsPerCycle) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);

  // Queue multiple commands
  for (int i = 0; i < 5; i++) {
    controller.handle_mqtt_command("modbus/relay/relay1/set", i % 2 == 0 ? "ON" : "OFF");
  }

  // Should process all commands
  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, _)).Times(5).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", _, true)).Times(5).WillRepeatedly(Return(true));

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, CommandQueueLimit) {
  // Set low limit
  polling_config_.max_commands_per_cycle = 2;

  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);

  // Queue 5 commands
  for (int i = 0; i < 5; i++) {
    controller.handle_mqtt_command("modbus/relay/relay1/set", "ON");
  }

  // Should only process 2 commands per cycle
  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, true)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", "ON", true)).Times(2).WillRepeatedly(Return(true));

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, UnknownRelayCommand) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);

  // Command for non-existent relay
  controller.handle_mqtt_command("modbus/relay/unknown_relay/set", "ON");

  // Should not call modbus or mqtt
  EXPECT_CALL(*mock_modbus_, write_coil(_, _, _)).Times(0);
  EXPECT_CALL(*mock_mqtt_, publish(_, _, _)).Times(0);

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, InvalidTopicFormat) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);

  // Invalid topic formats
  controller.handle_mqtt_command("invalid/topic", "ON");
  controller.handle_mqtt_command("modbus/relay/", "ON");
  controller.handle_mqtt_command("modbus/input/test/set", "ON");

  // Should not process any commands
  EXPECT_CALL(*mock_modbus_, write_coil(_, _, _)).Times(0);

  controller.process_relay_commands();
}

TEST_F(DeviceControllerTest, PayloadParsing) {
  DeviceController controller(inputs_, relays_, polling_config_, *mock_modbus_, *mock_mqtt_);

  // Test different payload formats for ON
  std::vector<std::string> on_payloads = {"ON", "1", "true"};
  for (const auto& payload : on_payloads) {
    controller.handle_mqtt_command("modbus/relay/relay1/set", payload);
  }

  // Test OFF payload
  controller.handle_mqtt_command("modbus/relay/relay1/set", "OFF");

  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, true)).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_modbus_, write_coil(1, 0, false)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(*mock_mqtt_, publish("test/relay1/state", _, true)).Times(4).WillRepeatedly(Return(true));

  controller.process_relay_commands();
}
