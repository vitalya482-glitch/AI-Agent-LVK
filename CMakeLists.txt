cmake_minimum_required(VERSION 3.20)

set(AI_AGENT_LVK_VERSION "0.2.0" CACHE STRING "AI-Agent-LVK version")
project(AI-Agent-LVK VERSION ${AI_AGENT_LVK_VERSION} LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AI-Agent-LVK is currently Windows-only.")
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file("${CMAKE_SOURCE_DIR}/app.update.json.in"
               "${CMAKE_BINARY_DIR}/generated/app.update.json" @ONLY)

add_executable(AI-Agent-LVK WIN32
    src/main.cpp
    src/config/ConfigManager.cpp
    src/dependencies/DependencyChecker.cpp
    src/docker/DockerManager.cpp
    src/launcher/LlamaManager.cpp
    src/memory/MemoryMonitor.cpp
    src/process/ProcessManager.cpp
    src/process/ProcessRunner.cpp
    src/process/BackgroundWorker.cpp
    src/log/LogManager.cpp
    src/port/PortChecker.cpp
    src/ui/ModelSettingsWindow.cpp
    src/update/UpdateManager.cpp
    src/update/UpdateCloseBridge.cpp
)

target_compile_features(AI-Agent-LVK PRIVATE cxx_std_20)
target_include_directories(AI-Agent-LVK PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(AI-Agent-LVK PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    AI_AGENT_LVK_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(AI-Agent-LVK PRIVATE user32 shell32 shlwapi winhttp ws2_32 psapi)
set_target_properties(AI-Agent-LVK PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

if(MSVC)
    target_compile_options(AI-Agent-LVK PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(AI-Agent-LVK PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET AI-Agent-LVK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/generated/app.update.json"
        "$<TARGET_FILE_DIR:AI-Agent-LVK>/app.update.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/docker"
        "$<TARGET_FILE_DIR:AI-Agent-LVK>/docker"
    COMMENT "Copying launcher runtime files"
)
