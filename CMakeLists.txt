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
set(CMAKE_AUTORCC ON)

# Export compile commands for IDE code intelligence
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6 dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Multimedia MultimediaWidgets TextToSpeech)

# Add target executable
add_executable(TitanAI
    src/main.cpp
    resources.qrc
    src/gui/main_window.cpp
    src/gui/voice_settings_dialog.cpp
    src/gui/camera_dialog.cpp
    src/agent/agent.cpp
    src/llm/ollama_client.cpp
    src/llm/ollama_manager.cpp
    src/tools/code_fixer.cpp
    src/tools/file_organizer.cpp
    src/tools/package_manager.cpp
    src/tools/system_info.cpp
    src/voice/audio_capture.cpp
    src/voice/speech_recognizer.cpp
    src/voice/text_to_speech.cpp
    src/voice/voice_engine.cpp
    src/learning/task_tracker.cpp
    src/learning/activity_analyzer.cpp
    src/learning/suggestion_engine.cpp
    src/calendar/calendar_manager.cpp
    src/calendar/notification_manager.cpp
    src/gui/calendar_settings_dialog.cpp
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
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::TextToSpeech
)

# Optional Vosk speech-to-text engine. When vosk_api.h and libvosk are found,
# real-time voice input is compiled in; otherwise the app still builds and the
# voice input feature is reported as unavailable at runtime.
find_path(VOSK_INCLUDE_DIR NAMES vosk_api.h)
find_library(VOSK_LIBRARY NAMES vosk)
if(VOSK_INCLUDE_DIR AND VOSK_LIBRARY)
    target_include_directories(TitanAI PRIVATE ${VOSK_INCLUDE_DIR})
    target_compile_definitions(TitanAI PRIVATE TITANAI_HAVE_VOSK=1)
    target_link_libraries(TitanAI PRIVATE ${VOSK_LIBRARY})
    message(STATUS "TitanAI: Vosk found - speech-to-text enabled")
else()
    message(WARNING "TitanAI: Vosk not found - speech-to-text disabled (install 'vosk-api' to enable)")
endif()
