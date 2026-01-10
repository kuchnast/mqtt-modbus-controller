#include "modbus_manager.hpp"
#include <iostream>
#include <thread>
#include <cstring>

ModbusManager::ModbusManager(const ModbusConfig& config)
    : config_(config), ctx_(nullptr), connected_(false),
      read_success_(0), read_errors_(0), write_success_(0), write_errors_(0) {
}

ModbusManager::~ModbusManager() {
    disconnect();
}

bool ModbusManager::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_) {
        return true;
    }
    
    ctx_ = modbus_new_rtu(
        config_.port.c_str(),
        config_.baudrate,
        config_.parity,
        config_.data_bits,
        config_.stop_bits
    );
    
    if (ctx_ == nullptr) {
        std::cerr << "Failed to create Modbus RTU context" << std::endl;
        return false;
    }
    
    if (modbus_set_slave(ctx_, 1) == -1) {
        std::cerr << "Failed to set Modbus slave" << std::endl;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    if (modbus_connect(ctx_) == -1) {
        std::cerr << "Modbus connection failed: " << modbus_strerror(errno) << std::endl;
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }
    
    // Set timeouts
    struct timeval response_timeout;
    response_timeout.tv_sec = 0;
    response_timeout.tv_usec = config_.response_timeout_ms * 1000;
    modbus_set_response_timeout(ctx_, response_timeout.tv_sec, response_timeout.tv_usec);
    
    struct timeval byte_timeout;
    byte_timeout.tv_sec = 0;
    byte_timeout.tv_usec = config_.byte_timeout_ms * 1000;
    modbus_set_byte_timeout(ctx_, byte_timeout.tv_sec, byte_timeout.tv_usec);
    
    connected_ = true;
    
    std::cout << "✓ Modbus RTU connected: " << config_.port 
              << " @ " << config_.baudrate << " baud" << std::endl;
    std::cout << "  Timeouts: " << config_.response_timeout_ms << "ms response, "
              << config_.byte_timeout_ms << "ms byte" << std::endl;
    
    return true;
}

void ModbusManager::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    
    connected_ = false;
}

bool ModbusManager::read_discrete_inputs(int slave_id, int start_addr, int count, uint8_t* dest) {
    return read_with_retry(slave_id, start_addr, count, dest);
}

bool ModbusManager::write_coil(int slave_id, int address, bool state) {
    return write_with_retry(slave_id, address, state);
}

bool ModbusManager::read_with_retry(int slave_id, int start_addr, int count, uint8_t* dest) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_ || !ctx_) {
        return false;
    }
    
    for (int retry = 0; retry < config_.max_retries; retry++) {
        modbus_set_slave(ctx_, slave_id);
        
        int rc = modbus_read_input_bits(ctx_, start_addr, count, dest);
        if (rc != -1) {
            read_success_++;
            return true;
        }
        
        if (retry < config_.max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
    
    read_errors_++;
    
    // Log errors periodically (not every time to avoid spam)
    static auto last_error_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_error_log).count() > 10) {
        std::cerr << "Modbus read error: slave " << slave_id 
                  << " addr " << start_addr << " count " << count
                  << " (after " << config_.max_retries << " retries): "
                  << modbus_strerror(errno) << std::endl;
        last_error_log = now;
    }
    
    return false;
}

bool ModbusManager::write_with_retry(int slave_id, int address, bool state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_ || !ctx_) {
        return false;
    }
    
    for (int retry = 0; retry < config_.max_retries; retry++) {
        modbus_set_slave(ctx_, slave_id);
        
        int rc = modbus_write_bit(ctx_, address, state ? 1 : 0);
        if (rc != -1) {
            write_success_++;
            return true;
        }
        
        if (retry < config_.max_retries - 1) {
            std::cerr << "Modbus write error: slave " << slave_id 
                      << " addr " << address << " (attempt " << (retry + 1)
                      << "/" << config_.max_retries << "): "
                      << modbus_strerror(errno) << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    write_errors_++;
    std::cerr << "CRITICAL: Failed to write coil slave " << slave_id 
              << " addr " << address << " after " << config_.max_retries 
              << " attempts!" << std::endl;
    
    return false;
}

ModbusManager::Stats ModbusManager::get_stats() const {
    return {
        read_success_.load(),
        read_errors_.load(),
        write_success_.load(),
        write_errors_.load()
    };
}

void ModbusManager::reset_stats() {
    read_success_ = 0;
    read_errors_ = 0;
    write_success_ = 0;
    write_errors_ = 0;
}