#pragma once

#include "config.hpp"
#include "i_modbus_manager.hpp"
#include "logger/logger.hpp"
#include <array>
#include <atomic>
#include <modbus/modbus.h>
#include <mutex>

struct ModbusManagerStats {
  int read_success;
  int read_errors;
  int write_success;
  int write_errors;

  ModbusManagerStats();
  ModbusManagerStats(int rs, int re, int ws, int we);
};

class ModbusManager : public IModbusManager {
public:
  explicit ModbusManager(const ModbusConfig &config);
  virtual ~ModbusManager();

  // Prevent copying
  ModbusManager(const ModbusManager &) = delete;
  ModbusManager &operator=(const ModbusManager &) = delete;

  bool connect() override;
  void disconnect() override;
  bool is_connected() const override { return connected_; }

  virtual bool read_discrete_inputs(int slave_id, int start_addr,
                                    std::array<uint8_t, 8> &dest) override;
  virtual bool write_coil(int slave_id, int address, bool state) override;

  std::unique_ptr<ModbusManagerStats> get_stats() const override;
  void reset_stats() override;

private:
  ModbusConfig config_;
  modbus_t *ctx_;
  bool connected_;
  mutable std::mutex mutex_;

  Logger logger_;

  std::atomic<int> read_success_;
  std::atomic<int> read_errors_;
  std::atomic<int> write_success_;
  std::atomic<int> write_errors_;

  bool read_with_retry(int slave_id, int start_addr,
                       std::array<uint8_t, 8> &dest);
  bool write_with_retry(int slave_id, int address, bool state);
};
