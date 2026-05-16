# Tests subdirectory CMake configuration

add_executable(tse_mbo_tests tse_mbo_tests.cpp)
target_include_directories(tse_mbo_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tse_mbo_tests PRIVATE tse_mbo_lib ${LIBPCAP_LIBRARIES})
if(nlohmann_json_FOUND)
    target_link_libraries(tse_mbo_tests PRIVATE nlohmann_json::nlohmann_json)
else()
    target_include_directories(tse_mbo_tests PRIVATE /usr/include)
endif()

add_test(NAME tse_mbo_tests COMMAND tse_mbo_tests)
