cmake_minimum_required(VERSION 3.20)

project(TitanAI
    VERSION 0.2.0
    DESCRIPTION "Lightweight local-first AI assistant for Arch Linux"
    LANGUAGES CXX
)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Qt Meta-Object Compiler (MOC)
set(CMAKE_AUTOMOC ON)

# Export compile commands for IDE code intelligence
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6 dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

# Add target executable
add_executable(TitanAI
    src/main.cpp
    src/gui/main_window.cpp
    src/agent/agent.cpp
    src/llm/ollama_client.cpp
    src/llm/ollama_manager.cpp
    src/tools/code_fixer.cpp
    src/tools/package_manager.cpp
    src/tools/system_info.cpp
)

# Enable compiler warnings
if(MSVC)
    target_compile_options(TitanAI PRIVATE /W4)
else()
    target_compile_options(TitanAI PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_features(TitanAI PRIVATE cxx_std_20)
target_include_directories(TitanAI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(TitanAI PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)
