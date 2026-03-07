cmake_minimum_required(VERSION 3.16)

project(HttpProxy
        DESCRIPTION "HTTP Proxy from https://csprimer.com/courses/networking/#http"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------
# Main library
# -----------------------
add_library(HttpProxy
    src/parser.cpp
)

target_include_directories(HttpProxy PUBLIC src)

add_executable(proxy src/proxy.cpp)
target_link_libraries(proxy PRIVATE HttpProxy)

# -----------------------
# Testing
# -----------------------
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(test)
