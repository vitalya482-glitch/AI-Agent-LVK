cmake_minimum_required(VERSION 3.20)

set(AI_AGENT_LVK_VERSION "0.1.5" CACHE STRING "AI-Agent-LVK version")
project(AI-Agent-LVK VERSION ${AI_AGENT_LVK_VERSION} LANGUAGES CXX)

option(AI_AGENT_LVK_ENABLE_CUDA "Enable CUDA when the CUDA Toolkit is installed" ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
if(AI_AGENT_LVK_ENABLE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(GGML_CUDA ON CACHE BOOL "" FORCE)
    endif()
endif()
add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_SOURCE_DIR}/app.update.json.in"
    "${CMAKE_BINARY_DIR}/generated/app.update.json"
    @ONLY
)

set(AI_AGENT_LVK_SOURCES
    src/main.cpp
    src/api/ApiServer.cpp
    src/core/CommandDispatcher.cpp
    src/model/ModelRuntime.cpp
    src/net/HttpServer.cpp
    src/update/UpdateManager.cpp
)

if(WIN32)
    list(APPEND AI_AGENT_LVK_SOURCES
        src/net/platform/WindowsSocket.cpp
        src/update/UpdateCloseBridge.cpp
    )
else()
    list(APPEND AI_AGENT_LVK_SOURCES
        src/net/platform/PosixSocket.cpp
    )
endif()

add_executable(AI-Agent-LVK ${AI_AGENT_LVK_SOURCES})

target_compile_features(AI-Agent-LVK PRIVATE cxx_std_20)
target_include_directories(AI-Agent-LVK PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(AI-Agent-LVK PRIVATE
    AI_AGENT_LVK_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(AI-Agent-LVK PRIVATE Threads::Threads llama)

if(WIN32)
    target_compile_definitions(AI-Agent-LVK PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(AI-Agent-LVK PRIVATE user32 ws2_32)
endif()

if(MSVC)
    target_compile_options(AI-Agent-LVK PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(AI-Agent-LVK PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET AI-Agent-LVK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/generated/app.update.json"
        "$<TARGET_FILE_DIR:AI-Agent-LVK>/app.update.json"
    COMMENT "Copying generated app.update.json"
)

if(WIN32)
    add_executable(AI-Agent-LVK-GUI WIN32
        gui/ChatWindow.cpp
        gui/main.cpp
        gui/ApiClient.cpp
    )

    target_compile_features(AI-Agent-LVK-GUI PRIVATE cxx_std_20)
    target_include_directories(AI-Agent-LVK-GUI PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/gui"
    )
    target_compile_definitions(AI-Agent-LVK-GUI PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        AI_AGENT_LVK_VERSION="${PROJECT_VERSION}"
    )
    target_link_libraries(AI-Agent-LVK-GUI PRIVATE user32 winhttp comdlg32 urlmon)

    if(MSVC)
        target_compile_options(AI-Agent-LVK-GUI PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
