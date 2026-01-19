#pragma once

#include <array>
#include <cstdint>

struct ModbusManagerStats;

class IModbusManager {
public:
    virtual ~IModbusManager() = default;
    
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    virtual bool read_discrete_inputs(int slave_id, int start_addr, std::array<uint8_t, 8>& dest) = 0;
    virtual bool write_coil(int slave_id, int address, bool state) = 0;
    
    virtual std::unique_ptr<ModbusManagerStats> get_stats() const = 0;
    virtual void reset_stats() = 0;
};