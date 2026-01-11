#pragma once

#include "config.hpp"
#include <logger/logger.hpp>
#include <modbus/modbus.h>
#include <atomic>
#include <array>
#include <mutex>

class ModbusManager {
public:
    explicit ModbusManager(const ModbusConfig& config);
    ~ModbusManager();
    
    // Prevent copying
    ModbusManager(const ModbusManager&) = delete;
    ModbusManager& operator=(const ModbusManager&) = delete;
    
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    bool read_discrete_inputs(int slave_id, int start_addr, std::array<uint8_t, 8>& dest);
    bool write_coil(int slave_id, int address, bool state);
    
    //TODO: Move stats to a separate class?
    // Statistics
    struct Stats {
        int read_success;
        int read_errors;
        int write_success;
        int write_errors;
    };
    
    Stats get_stats() const;
    void reset_stats();
    
private:
    ModbusConfig config_;
    modbus_t* ctx_;
    bool connected_;
    mutable std::mutex mutex_;

    Logger logger_;
    
    std::atomic<int> read_success_;
    std::atomic<int> read_errors_;
    std::atomic<int> write_success_;
    std::atomic<int> write_errors_;
    
    bool read_with_retry(int slave_id, int start_addr, std::array<uint8_t, 8>& dest);
    bool write_with_retry(int slave_id, int address, bool state);
};
