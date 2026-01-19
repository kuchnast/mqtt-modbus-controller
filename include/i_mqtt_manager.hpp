#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

using MqttMessageCallback =
    std::function<void(const std::string &topic, const std::string &payload)>;
struct MqttManagerStats;

class IMqttManager {
public:
  virtual ~IMqttManager() = default;

  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual bool is_connected() const = 0;

  virtual bool subscribe(const std::string &topic) = 0;
  virtual bool publish(const std::string &topic, const std::string &payload,
                       bool retained = true) = 0;

  virtual void set_message_callback(MqttMessageCallback callback) = 0;

  virtual std::unique_ptr<MqttManagerStats> get_stats() const = 0;
  virtual void reset_stats() = 0;
};
