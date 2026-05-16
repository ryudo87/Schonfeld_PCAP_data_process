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

# Add the executable
add_executable(tse_mbo_processor main.cpp)

# Link libraries
target_link_libraries(tse_mbo_processor PRIVATE ${LIBPCAP_LIBRARIES})

# For nlohmann/json
if(nlohmann_json_FOUND)
    target_link_libraries(tse_mbo_processor PRIVATE nlohmann_json::nlohmann_json)
else()
    # System header fallback
    target_include_directories(tse_mbo_processor PRIVATE /usr/include)
endif()

# Installation
install(TARGETS tse_mbo_processor DESTINATION bin)
