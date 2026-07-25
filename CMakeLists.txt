cmake_minimum_required(VERSION 3.20)

project(TitanAI
    VERSION 0.1.0
    DESCRIPTION "Lightweight local-first AI assistant for Arch Linux"
    LANGUAGES CXX
)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE code intelligence
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add target executable
add_executable(TitanAI src/main.cpp)

# Enable compiler warnings and modern C++ features
if(MSVC)
    target_compile_options(TitanAI PRIVATE /W4)
else()
    target_compile_options(TitanAI PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_features(TitanAI PRIVATE cxx_std_20)
target_include_directories(TitanAI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
