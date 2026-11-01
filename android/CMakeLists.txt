cmake_minimum_required(VERSION 3.22)
project(lain_android LANGUAGES C CXX ASM)

if(NOT ANDROID)
    message(FATAL_ERROR "This CMake project must be configured by the Android NDK")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(LAIN_ANDROID_SDL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.deps/SDL2-2.32.10")
if(NOT EXISTS "${LAIN_ANDROID_SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL2 source is missing. Open this project with Gradle once so android/settings.gradle can fetch it.")
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../port" "${CMAKE_BINARY_DIR}/lain-port")
