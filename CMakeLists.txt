cmake_minimum_required(VERSION 3.10)
project(modbus_mqtt_gateway VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "===============================================")
message(STATUS "  Modbus RTU to MQTT Gateway v${PROJECT_VERSION}")
message(STATUS "===============================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")

# Options
option(BUILD_TESTS "Build unit tests with GTest" ON)
option(ENABLE_COVERAGE "Enable code coverage analysis" ON)

# =====================================
# Find Dependencies
# =====================================

find_package(PkgConfig REQUIRED)

# Modbus library
pkg_check_modules(MODBUS REQUIRED libmodbus)
message(STATUS "✓ Found libmodbus: ${MODBUS_VERSION}")

# MQTT C++ library
find_path(PAHO_MQTT_CPP_INCLUDE_DIR 
    NAMES mqtt/async_client.h
    PATHS /usr/include /usr/local/include
)

find_library(PAHO_MQTT_CPP_LIBRARY
    NAMES paho-mqttpp3 libpaho-mqttpp3
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(PAHO_MQTT_CPP_INCLUDE_DIR AND PAHO_MQTT_CPP_LIBRARY)
    message(STATUS "✓ Found paho-mqtt-cpp: ${PAHO_MQTT_CPP_LIBRARY}")
else()
    message(FATAL_ERROR "paho-mqtt-cpp not found! Install: sudo apt-get install libpaho-mqttpp-dev")
endif()

# JSON library
find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATHS /usr/include /usr/local/include
)

if(NLOHMANN_JSON_INCLUDE_DIR)
    message(STATUS "✓ Found nlohmann-json: ${NLOHMANN_JSON_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "nlohmann-json not found! Install: sudo apt-get install nlohmann-json3-dev")
endif()

# Threads
find_package(Threads REQUIRED)
message(STATUS "✓ Found Threads")

# GTest (only if building tests)
if(BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTEST_FOUND)
        # Try to find GMock which includes GTest
        find_package(GMock QUIET)
        if(GMOCK_FOUND)
            set(GTEST_FOUND TRUE)
            set(GTEST_INCLUDE_DIRS ${GMOCK_INCLUDE_DIRS})
            set(GTEST_LIBRARIES ${GMOCK_LIBRARIES})
        endif()
    endif()
    
    if(GTEST_FOUND)
        message(STATUS "✓ Found GTest(${GTEST_VERSION})/GMock(${GMOCK_VERSION})")
        enable_testing()
    else()
        message(WARNING "GTest/GMock not found! Tests will not be built.")
        set(BUILD_TESTS OFF)
    endif()
endif()

message(STATUS "===============================================")

# =====================================
# Source Files
# =====================================

set(SOURCES
    src/logger/logger.cpp 
    src/config.cpp
    src/modbus_manager.cpp
    src/mqtt_manager.cpp
    src/device_controller.cpp
    src/application.cpp
)

set(HEADERS
    include/logger/logger.hpp
    include/config.hpp
    include/modbus_manager.hpp
    include/i_modbus_manager.hpp
    include/i_mqtt_manager.hpp
    include/mqtt_manager.hpp
    include/device_controller.hpp
    include/application.hpp
)

# =====================================
# Main Executable
# =====================================

add_executable(modbus_poller 
    src/main.cpp
    ${SOURCES}
)

target_include_directories(modbus_poller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MODBUS_INCLUDE_DIRS}
    ${PAHO_MQTT_CPP_INCLUDE_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(modbus_poller PRIVATE
    ${MODBUS_LIBRARIES}
    ${PAHO_MQTT_CPP_LIBRARY}
    paho-mqtt3as
    Threads::Threads
)

# Compiler warnings and optimizations
target_compile_options(modbus_poller PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Add version defines
target_compile_definitions(modbus_poller PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_NAME="${PROJECT_NAME}"
)

message(STATUS "Main executable: modbus_poller")

# =====================================
# Unit Tests
# =====================================

if(BUILD_TESTS AND GTEST_FOUND)
    message(STATUS "Building tests enabled")
    
    # Mock headers
    set(MOCK_HEADERS
        tests/mocks/modbus_manager_mock.hpp
        tests/mocks/mqtt_manager_mock.hpp
    )
    
    # Test sources
    set(TEST_SOURCES
        tests/test_config.cpp
        tests/test_device_controller.cpp
        tests/test_edge_cases.cpp
        tests/run_tests.cpp
    )
    
    # Test executable
    add_executable(modbus_tests
        ${MOCK_HEADERS}
        ${TEST_SOURCES}
        ${SOURCES}
    )
    
    target_include_directories(modbus_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/mocks
        ${MODBUS_INCLUDE_DIRS}
        ${PAHO_MQTT_CPP_INCLUDE_DIR}
        ${NLOHMANN_JSON_INCLUDE_DIR}
        ${GTEST_INCLUDE_DIRS}
    )
    
    message(STATUS "${GMOCK_LIBRARIES}")

    target_link_libraries(modbus_tests PRIVATE
        ${MODBUS_LIBRARIES}
        ${PAHO_MQTT_CPP_LIBRARY}
        paho-mqtt3as
        Threads::Threads
        ${GTEST_LIBRARIES}
        GTest::gmock
    )
    
    target_compile_options(modbus_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -g
    )
    
    # Coverage (if enabled)
    if(ENABLE_COVERAGE)
        message(STATUS "Code coverage enabled")
        target_compile_options(modbus_tests PRIVATE --coverage)
        target_link_options(modbus_tests PRIVATE --coverage)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND}
            COMMAND lcov
                --capture
                --directory .
                --output-file coverage.info
                --ignore-errors inconsistent,unused
            COMMAND lcov
                --remove coverage.info
                '/usr/*'
                '*/tests/*'
                '*gtest*'
                '*gmock*'
                --output-file coverage.cleaned.info
                --ignore-errors inconsistent,unused
            COMMAND genhtml
                coverage.cleaned.info
                --output-directory coverage
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
        )
    endif()
    
    # Add tests to CTest
    include(GoogleTest)
    gtest_discover_tests(modbus_tests)
    
    # Custom test targets
    add_custom_target(test-verbose
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/modbus_tests --gtest_color=yes
        DEPENDS modbus_tests
        COMMENT "Running tests with verbose output"
    )
    
    add_custom_target(test-filter
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/modbus_tests --gtest_filter=*
        DEPENDS modbus_tests
        COMMENT "Running filtered tests"
    )
    
    message(STATUS "Test executable: modbus_tests")
    message(STATUS "Run with: make test or ./modbus_tests")
else()
    message(STATUS "Tests disabled (BUILD_TESTS=${BUILD_TESTS})")
endif()

# =====================================
# Installation
# =====================================

# Install executable
install(TARGETS modbus_poller
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

# Install configuration template
install(FILES config.json
    DESTINATION /etc/modbus_poller
    COMPONENT config
)

# Install systemd service file
install(FILES systemd/modbus_poller.service
    DESTINATION /etc/systemd/system
    COMPONENT systemd
    OPTIONAL
)

# Install headers (optional, for library use)
install(FILES ${HEADERS}
    DESTINATION include/modbus_poller
    COMPONENT development
    OPTIONAL
)

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# =====================================
# Documentation & Helper Targets
# =====================================

# Custom target: format (requires clang-format)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i -style=.clang_format ${SOURCES} ${HEADERS} src/main.cpp ${TEST_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting source code with clang-format"
    )
    message(STATUS "Target 'format' available (clang-format found)")
endif()

# Custom target: clean-all (deep clean)
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing entire build directory"
)

# Custom target: install-deps (Ubuntu/Debian)
add_custom_target(install-deps
    COMMAND sudo apt-get update
    COMMAND sudo apt-get install -y 
        libmodbus-dev 
        libpaho-mqtt-dev 
        libpaho-mqttpp-dev 
        nlohmann-json3-dev
        libgtest-dev
        libgmock-dev
        cmake
    COMMENT "Installing system dependencies"
)

# =====================================
# CPack Configuration (Packaging)
# =====================================

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Your Company")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modbus RTU to MQTT Gateway")
set(CPACK_PACKAGE_DESCRIPTION "Professional gateway bridging Modbus RTU devices with MQTT broker")
set(CPACK_PACKAGE_CONTACT "your-email@example.com")

# DEB package specific
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libmodbus5, libpaho-mqtt1.3, libpaho-mqttpp3, libc6, libstdc++6")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# RPM package specific
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")

set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")

include(CPack)

# =====================================
# Summary
# =====================================

message(STATUS "===============================================")
message(STATUS "Configuration Summary:")
message(STATUS "  Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "===============================================")
message(STATUS "Available targets:")
message(STATUS "  make                - Build main executable")
if(BUILD_TESTS)
message(STATUS "  make modbus_tests   - Build test executable (if enabled)")
message(STATUS "  make test           - Run tests (if enabled)")
endif()
message(STATUS "  make install        - Install to system")
message(STATUS "  make package        - Create installation package")
if(CLANG_FORMAT)
message(STATUS "  make format         - Format source code")
endif()
if(ENABLE_COVERAGE)
message(STATUS "  make coverage       - Generate code coverage report")
endif()
message(STATUS "  make clean-all      - Deep clean")
message(STATUS "  make install-deps   - Install dependencies (Ubuntu/Debian)")
message(STATUS "===============================================")
message(STATUS "Build commands:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j$(nproc)")
message(STATUS "===============================================")