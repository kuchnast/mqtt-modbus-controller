#include "application.hpp"
#include <csignal>
#include <iostream>
#include <atomic>
#include <thread>

std::atomic<bool> g_running(true);
std::atomic<bool> g_force_exit(false);

void signal_handler(int signum) {
    static int signal_count = 0;
    signal_count++;
    
    std::cout << "\n[SIGNAL " << signum << "] ";
    
    if (signal_count == 1) {
        std::cout << "Shutting down gracefully... (Ctrl+C again to force)" << std::endl;
        g_running = false;
    } else {
        std::cout << "FORCING EXIT!" << std::endl;
        g_force_exit = true;
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "Terminating process..." << std::endl;
            _exit(1);
        }).detach();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Modbus RTU ↔ MQTT Gateway v3.0" << std::endl;
    std::cout << "Professional Edition with JSON Config" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::string config_file = "config.json";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    try {
        Application app(config_file);
        
        if (!app.initialize()) {
            std::cerr << "Failed to initialize application" << std::endl;
            return 1;
        }
        
        app.run(g_running, g_force_exit);
        app.shutdown();
        
        std::cout << "Application terminated successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}