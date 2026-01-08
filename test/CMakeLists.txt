cmake_minimum_required(VERSION 3.10)
project(modbus_poller)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Znajdź biblioteki
find_package(PkgConfig REQUIRED)
pkg_check_modules(MODBUS REQUIRED libmodbus)
find_package(PahoMqttCpp REQUIRED)

# Dodaj executable
add_executable(modbus_poller modbus_poller.cpp)

# Linkuj biblioteki
target_include_directories(modbus_poller PRIVATE 
    ${MODBUS_INCLUDE_DIRS}
    ${PAHO_MQTT_CPP_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(modbus_poller 
    ${MODBUS_LIBRARIES}
    ${PAHO_MQTT_C_LIBRARIES}
    ${PAHO_MQTT_CPP_LIBRARIES}
    PahoMqttCpp::paho-mqttpp3
    pthread
)

# Instalacja
# install(TARGETS modbus_poller DESTINATION /usr/local/bin)