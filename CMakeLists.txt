cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17) # Compile as C++17.
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Require C++17 support.
set(CLANG_VERSION 15)

project(HttpProxy
        DESCRIPTION "HTTP Proxy from https://csprimer.com/courses/networking/#http"
        LANGUAGES C CXX
)

# Default to Debug mode
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(STATUS "Setting build type to `Debug` as none was specified.")
        set(CMAKE_BUILD_TYPE "Debug")
endif()

add_executable(proxy src/proxy.cpp)
