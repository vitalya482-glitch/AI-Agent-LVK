cmake_minimum_required(VERSION 3.20)

project(AI-Agent-LVK VERSION 0.1.0 LANGUAGES CXX)

add_executable(AI-Agent-LVK
    src/main.cpp
    src/update/UpdateManager.cpp
    src/update/UpdateCloseBridge.cpp
)

target_compile_features(AI-Agent-LVK PRIVATE cxx_std_20)

target_compile_definitions(AI-Agent-LVK PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    AI_AGENT_LVK_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(AI-Agent-LVK PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(AI-Agent-LVK PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(AI-Agent-LVK PRIVATE user32)

add_custom_command(TARGET AI-Agent-LVK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/app.update.json"
        "$<TARGET_FILE_DIR:AI-Agent-LVK>/app.update.json"
    COMMENT "Copying app.update.json"
)
