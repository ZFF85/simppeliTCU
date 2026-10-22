cmake_minimum_required(VERSION 3.14)
project(simppeliTCU_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest QUIET)
if(GTest_FOUND)
    set(GTEST_MAIN_TARGET GTest::gtest_main)
else()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    set(GTEST_MAIN_TARGET gtest_main)
endif()

add_library(test_stubs STATIC
    test_canInterface_stubs.cpp
)

target_include_directories(test_stubs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(can_leaf_ze1_parsing_tests
    test_canLeafZE1_parsing.cpp
    ../simppeliTCU/canLeafZE1.cpp
    ../simppeliTCU/vehicleTypes.h
    ../simppeliTCU/canInterface.h
    ../simppeliTCU/canLeafZE1.h
)

target_include_directories(can_leaf_ze1_parsing_tests PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../simppeliTCU
)

target_link_libraries(can_leaf_ze1_parsing_tests PRIVATE 
    ${GTEST_MAIN_TARGET}
    test_stubs
)

enable_testing()

add_executable(uplink_policy_tests test_uplinkPolicy.cpp)
target_include_directories(uplink_policy_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../simppeliTCU)
add_test(NAME uplink_policy_tests COMMAND uplink_policy_tests)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CJSON QUIET IMPORTED_TARGET libcjson)
endif()
if(TARGET PkgConfig::CJSON)
    set(CJSON_TEST_TARGET PkgConfig::CJSON)
else()
    include(FetchContent)
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable upstream cJSON tests" FORCE)
    FetchContent_Declare(cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.19
    )
    FetchContent_MakeAvailable(cjson)
    set(CJSON_TEST_TARGET cjson)
endif()
add_executable(remote_config_tests test_remoteConfigProtocol.cpp ../simppeliTCU/remoteConfigProtocol.cpp)
target_include_directories(remote_config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../simppeliTCU)
target_link_libraries(remote_config_tests PRIVATE ${GTEST_MAIN_TARGET} ${CJSON_TEST_TARGET})
add_test(NAME remote_config_tests COMMAND remote_config_tests)

add_executable(remote_config_controller_tests test_remoteConfigController.cpp
    ../simppeliTCU/remoteConfig.cpp ../simppeliTCU/remoteConfigProtocol.cpp)
target_include_directories(remote_config_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/remote_mocks ${CMAKE_CURRENT_SOURCE_DIR}/../simppeliTCU)
target_link_libraries(remote_config_controller_tests PRIVATE ${GTEST_MAIN_TARGET} ${CJSON_TEST_TARGET})
add_test(NAME remote_config_controller_tests COMMAND remote_config_controller_tests)

add_test(NAME can_leaf_ze1_parsing_tests COMMAND can_leaf_ze1_parsing_tests)
