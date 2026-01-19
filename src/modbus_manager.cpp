#include "modbus_manager.hpp"
#include <cstring>
#include <iostream>
#include <thread>

ModbusManagerStats::ModbusManagerStats()
    : read_success(0), read_errors(0), write_success(0), write_errors(0) {}

ModbusManagerStats::ModbusManagerStats(int rs, int re, int ws, int we)
    : read_success(rs), read_errors(re), write_success(ws), write_errors(we) {}

ModbusManager::ModbusManager(const ModbusConfig &config)
    : config_(config), ctx_(nullptr), connected_(false),
      logger_("ModbusManager"), read_success_(0), read_errors_(0),
      write_success_(0), write_errors_(0) {}

ModbusManager::~ModbusManager() { disconnect(); }

bool ModbusManager::connect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (connected_) {
    return true;
  }

  ctx_ = modbus_new_rtu(config_.port.c_str(), config_.baudrate, config_.parity,
                        config_.data_bits, config_.stop_bits);

  if (ctx_ == nullptr) {
    logger_.critical() << "Failed to create Modbus RTU context";
    return false;
  }

  if (modbus_set_slave(ctx_, 1) == -1) {
    logger_.critical() << "Failed to set Modbus slave";
    modbus_free(ctx_);
    ctx_ = nullptr;
    return false;
  }

  if (modbus_connect(ctx_) == -1) {
    logger_.critical() << "Modbus connection failed: "
                       << modbus_strerror(errno);
    modbus_free(ctx_);
    ctx_ = nullptr;
    return false;
  }

  // Set timeouts
  struct timeval response_timeout;
  response_timeout.tv_sec = 0;
  response_timeout.tv_usec = config_.response_timeout_ms * 1000;
  modbus_set_response_timeout(ctx_, response_timeout.tv_sec,
                              response_timeout.tv_usec);

  struct timeval byte_timeout;
  byte_timeout.tv_sec = 0;
  byte_timeout.tv_usec = config_.byte_timeout_ms * 1000;
  modbus_set_byte_timeout(ctx_, byte_timeout.tv_sec, byte_timeout.tv_usec);

  connected_ = true;

  logger_.info() << "Modbus RTU connected: " << config_.port << " @ "
                 << config_.baudrate << " baud"
                 << "  Timeouts: " << config_.response_timeout_ms
                 << "ms response, " << config_.byte_timeout_ms << "ms byte";

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

bool ModbusManager::read_discrete_inputs(int slave_id, int start_addr,
                                         std::array<uint8_t, 8> &dest) {
  return read_with_retry(slave_id, start_addr, dest);
}

bool ModbusManager::write_coil(int slave_id, int address, bool state) {
  return write_with_retry(slave_id, address, state);
}

bool ModbusManager::read_with_retry(int slave_id, int start_addr,
                                    std::array<uint8_t, 8> &dest) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !ctx_) {
    return false;
  }

  for (int retry = 0; retry < config_.max_retries; retry++) {
    modbus_set_slave(ctx_, slave_id);

    int rc = modbus_read_input_bits(ctx_, start_addr, dest.size(), dest.data());
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
  if (std::chrono::duration_cast<std::chrono::seconds>(now - last_error_log)
          .count() > 10) {
    logger_.error() << "Modbus read error: slave " << slave_id << " addr "
                    << start_addr << " count " << dest.size() << " (after "
                    << config_.max_retries
                    << " retries): " << modbus_strerror(errno);
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
      logger_.warning() << "Modbus write error: slave " << slave_id << " addr "
                        << address << " (attempt " << (retry + 1) << "/"
                        << config_.max_retries
                        << "): " << modbus_strerror(errno);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  write_errors_++;
  logger_.error() << "Failed to write coil slave " << slave_id << " addr "
                  << address << " after " << config_.max_retries
                  << " attempts!";

  return false;
}

std::unique_ptr<ModbusManagerStats> ModbusManager::get_stats() const {
  return std::make_unique<ModbusManagerStats>(
      read_success_.load(), read_errors_.load(), write_success_.load(),
      write_errors_.load());
}

void ModbusManager::reset_stats() {
  read_success_ = 0;
  read_errors_ = 0;
  write_success_ = 0;
  write_errors_ = 0;
}