#include "application.hpp"
#include "logger/logger.hpp"
#include <atomic>
#include <csignal>
#include <thread>

std::atomic<bool> g_running(true);
std::atomic<bool> g_force_exit(false);
Logger main_logger("Main");

void signal_handler(int signum) {
  static int signal_count = 0;
  signal_count++;

  if (signal_count == 1) {
    main_logger.info() << "Received signal " << signum
                       << ", shutting down gracefully...";
    g_running = false;
  } else {
    main_logger.critical() << "Received signal " << signum
                           << " again, FORCING EXIT!";
    g_force_exit = true;
    std::thread([]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      main_logger.critical() << "Terminating process...";
      _exit(1);
    }).detach();
  }
}

int main(int argc, char *argv[]) {
  Logger::enable_timestamps(true);
  Logger::enable_colors(true);
  Logger::set_global_level(LogLevel::LEVEL_DEBUG);

  main_logger.info() << "========================================";
  main_logger.info() << "Modbus RTU ↔ MQTT Gateway v3.0";
  main_logger.info() << "Professional Edition with JSON Config";
  main_logger.info() << "========================================";

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  std::string config_file = "config.json";
  if (argc > 1) {
    config_file = argv[1];
  }

  main_logger.debug() << "Using config file: " << config_file;

  try {
    Application app(config_file);

    if (!app.initialize()) {
      main_logger.error() << "Failed to initialize application";
      return 1;
    }

    app.run(g_running, g_force_exit);
    app.shutdown();

    main_logger.info() << "Application terminated successfully";
    return 0;

  } catch (const std::exception &e) {
    main_logger.critical() << "Fatal error: " << e.what();
    return 1;
  }
}