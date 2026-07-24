cmake_minimum_required(VERSION 3.20)

project(makxd-net-cpp VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "makxd-net-cpp currently targets the Windows MAKXD Bridge")
endif()

add_library(makxd-net-cpp STATIC
    src/makxd_net.cpp
)

target_include_directories(makxd-net-cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(makxd-net-cpp PUBLIC cxx_std_23)
target_compile_definitions(makxd-net-cpp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(makxd-net-cpp PUBLIC ws2_32)

if(MSVC)
    target_compile_options(makxd-net-cpp PRIVATE /W4 /permissive-)
else()
    target_compile_options(makxd-net-cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(makxd-net-example examples/basic.cpp)
target_link_libraries(makxd-net-example PRIVATE makxd-net-cpp)

include(CTest)
if(BUILD_TESTING)
    add_executable(makxd-net-tests tests/net_client_tests.cpp)
    target_link_libraries(makxd-net-tests PRIVATE makxd-net-cpp)
    if(MSVC)
        target_compile_options(makxd-net-tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME makxd-net-tests COMMAND makxd-net-tests)
endif()

install(TARGETS makxd-net-cpp ARCHIVE DESTINATION lib)
install(FILES include/makxd_net.h DESTINATION include)
