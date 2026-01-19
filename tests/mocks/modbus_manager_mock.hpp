#pragma once

#include "i_modbus_manager.hpp"
#include <gmock/gmock.h>

// Mock implementation
class MockModbusManager : public IModbusManager {
public:
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, is_connected, (), (const, override));
    
    MOCK_METHOD(bool, read_discrete_inputs, (int slave_id, int start_addr, (std::array<uint8_t, 8>&) dest), (override));

    MOCK_METHOD(bool, write_coil, (int slave_id, int address, bool state), (override));
    
    MOCK_METHOD(std::unique_ptr<ModbusManagerStats>, get_stats, (), (const, override));
    MOCK_METHOD(void, reset_stats, (), (override));
};
