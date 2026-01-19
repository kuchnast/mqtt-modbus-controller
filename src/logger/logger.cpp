#include "logger/logger.hpp"

LogLevel Logger::global_level_ = LogLevel::LEVEL_INFO;
bool Logger::timestamps_enabled_ = true;
bool Logger::colors_enabled_ = true;
std::mutex Logger::mutex_;

Logger::Logger(const std::string &context)
    : context_(context), instance_level_(LogLevel::LEVEL_DEBUG) {}

void Logger::set_global_level(LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  global_level_ = level;
}

LogLevel Logger::get_global_level() {
  std::lock_guard<std::mutex> lock(mutex_);
  return global_level_;
}

void Logger::set_level(LogLevel level) { instance_level_ = level; }

LogLevel Logger::get_level() const { return instance_level_; }

void Logger::enable_timestamps(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  timestamps_enabled_ = enable;
}

void Logger::enable_colors(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  colors_enabled_ = enable;
}

Logger::LogStream Logger::debug() {
  return LogStream(*this, LogLevel::LEVEL_DEBUG);
}

Logger::LogStream Logger::info() {
  return LogStream(*this, LogLevel::LEVEL_INFO);
}

Logger::LogStream Logger::warning() {
  return LogStream(*this, LogLevel::LEVEL_WARNING);
}

Logger::LogStream Logger::error() {
  return LogStream(*this, LogLevel::LEVEL_ERROR);
}

Logger::LogStream Logger::critical() {
  return LogStream(*this, LogLevel::LEVEL_CRITICAL);
}

void Logger::log(LogLevel level, const std::string &message) {
  if (should_log(level)) {
    write_log(level, message);
  }
}

bool Logger::should_log(LogLevel level) const {
  return level >= instance_level_ && level >= global_level_;
}

std::string Logger::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::ostringstream oss;
  oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

  return oss.str();
}

std::string Logger::get_level_string(LogLevel level) const {
  switch (level) {
  case LogLevel::LEVEL_DEBUG:
    return "DEBUG";
  case LogLevel::LEVEL_INFO:
    return "INFO";
  case LogLevel::LEVEL_WARNING:
    return "WARNING";
  case LogLevel::LEVEL_ERROR:
    return "ERROR";
  case LogLevel::LEVEL_CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

std::string Logger::get_level_color(LogLevel level) const {
  if (!colors_enabled_) {
    return "";
  }

  switch (level) {
  case LogLevel::LEVEL_DEBUG:
    return "\033[36m"; // Cyan
  case LogLevel::LEVEL_INFO:
    return "\033[32m"; // Green
  case LogLevel::LEVEL_WARNING:
    return "\033[33m"; // Yellow
  case LogLevel::LEVEL_ERROR:
    return "\033[31m"; // Red
  case LogLevel::LEVEL_CRITICAL:
    return "\033[1;31m"; // Bold Red
  default:
    return "\033[0m"; // Reset
  }
}

void Logger::write_log(LogLevel level, const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostream &out = (level >= LogLevel::LEVEL_ERROR) ? std::cerr : std::cout;

  if (colors_enabled_) {
    out << get_level_color(level);
  }

  if (timestamps_enabled_) {
    out << "[" << get_timestamp() << "] ";
  }

  out << "[" << std::setw(8) << std::left << get_level_string(level) << "] ";

  if (!context_.empty()) {
    out << "[" << context_ << "] ";
  }

  out << message;

  if (colors_enabled_) {
    out << "\033[0m";
  }

  out << std::endl;
}

Logger::LogStream::LogStream(Logger &logger, LogLevel level)
    : logger_(logger), level_(level), should_log_(logger.should_log(level)) {}

Logger::LogStream::~LogStream() {
  if (should_log_) {
    logger_.write_log(level_, stream_.str());
  }
}