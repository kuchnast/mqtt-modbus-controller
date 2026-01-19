#include "modbus_manager.hpp"
#include <gtest/gtest.h>

class ModbusManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = "/dev/ttyUSB0";
        config_.baudrate = 9600;
        config_.parity = 'N';
        config_.data_bits = 8;
        config_.stop_bits = 1;
        config_.response_timeout_ms = 300;
        config_.byte_timeout_ms = 100;
        config_.max_retries = 3;
    }
    
    ModbusConfig config_;
};

TEST_F(ModbusManagerTest, ConstructorDestructor) {
    ASSERT_NO_THROW({
        ModbusManager manager(config_);
    });
}

TEST_F(ModbusManagerTest, InitialState) {
    ModbusManager manager(config_);
    
    EXPECT_FALSE(manager.is_connected());
}

TEST_F(ModbusManagerTest, Statistics) {
    ModbusManager manager(config_);
    
    auto stats = manager.get_stats();
    EXPECT_EQ(stats->read_success, 0);
    EXPECT_EQ(stats->read_errors, 0);
    EXPECT_EQ(stats->write_success, 0);
    EXPECT_EQ(stats->write_errors, 0);
}

TEST_F(ModbusManagerTest, ResetStatistics) {
    ModbusManager manager(config_);
    
    manager.reset_stats();
    
    auto stats = manager.get_stats();
    EXPECT_EQ(stats->read_success, 0);
    EXPECT_EQ(stats->read_errors, 0);
}
