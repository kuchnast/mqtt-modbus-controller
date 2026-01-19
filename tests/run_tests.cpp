#include "logger/logger.hpp"
#include <gtest/gtest.h>

int main(int argc, char **argv) {
  Logger::set_global_level(LogLevel::LEVEL_DEBUG);
  Logger::enable_timestamps(false);
  Logger::enable_colors(true);

  ::testing::InitGoogleTest(&argc, argv);

  std::cout << "Modbus-MQTT Gateway Test Suite\n";

  int result = RUN_ALL_TESTS();

  return result;
}