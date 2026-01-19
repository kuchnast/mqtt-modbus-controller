#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel {
  LEVEL_DEBUG = 0,
  LEVEL_INFO = 1,
  LEVEL_WARNING = 2,
  LEVEL_ERROR = 3,
  LEVEL_CRITICAL = 4
};

class Logger {
public:
  explicit Logger(const std::string &context = "");

  // Setting global log level
  static void set_global_level(LogLevel level);
  static LogLevel get_global_level();

  // Setting instance log level
  void set_level(LogLevel level);
  LogLevel get_level() const;

  // Enable/disable timestamps and colors
  static void enable_timestamps(bool enable);
  static void enable_colors(bool enable);

  class LogStream {
  public:
    LogStream(Logger &logger, LogLevel level);
    ~LogStream();

    template <typename T> LogStream &operator<<(const T &value) {
      if (should_log_) {
        stream_ << value;
      }
      return *this;
    }

    // Specialization for manipulators like std::endl
    LogStream &operator<<(std::ostream &(*manip)(std::ostream &)) {
      if (should_log_) {
        stream_ << manip;
      }
      return *this;
    }

  private:
    Logger &logger_;
    LogLevel level_;
    bool should_log_;
    std::ostringstream stream_;
  };

  LogStream debug();
  LogStream info();
  LogStream warning();
  LogStream error();
  LogStream critical();

  // Heper method to log directly
  void log(LogLevel level, const std::string &message);

private:
  std::string context_;
  LogLevel instance_level_;

  static LogLevel global_level_;
  static bool timestamps_enabled_;
  static bool colors_enabled_;
  static std::mutex mutex_;

  bool should_log(LogLevel level) const;
  std::string get_timestamp() const;
  std::string get_level_string(LogLevel level) const;
  std::string get_level_color(LogLevel level) const;
  void write_log(LogLevel level, const std::string &message);

  friend class LogStream;
};