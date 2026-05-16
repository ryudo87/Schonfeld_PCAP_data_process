cmake_minimum_required(VERSION 3.10)
project(tse_mbo_processor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPCAP REQUIRED libpcap)

# Include directories
include_directories(${LIBPCAP_INCLUDE_DIRS})

# Try to find nlohmann/json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via find_package, will try system headers")
endif()

enable_testing()

# Shared library for reuse in the main program and tests
add_library(tse_mbo_lib STATIC src/app.cpp)
target_include_directories(tse_mbo_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(nlohmann_json_FOUND)
    target_link_libraries(tse_mbo_lib PRIVATE nlohmann_json::nlohmann_json)
else()
    target_include_directories(tse_mbo_lib PRIVATE /usr/include)
endif()
target_link_libraries(tse_mbo_lib PRIVATE ${LIBPCAP_LIBRARIES})

add_executable(tse_mbo_processor src/main.cpp)
target_link_libraries(tse_mbo_processor PRIVATE tse_mbo_lib)

add_subdirectory(tests)

# Installation
install(TARGETS tse_mbo_processor DESTINATION bin)
