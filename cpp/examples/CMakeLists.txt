cmake_minimum_required(VERSION 3.20)
project(makxd-examples)

# Try to find makxd-cpp package first (if installed)
find_package(makxd-cpp QUIET)

if(NOT makxd-cpp_FOUND)
    # If not found, look for local build in parent directory
    message(STATUS "makxd-cpp package not found, looking for local build...")
    
    # Check if parent build exists
    get_filename_component(PARENT_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build" ABSOLUTE)
    if(EXISTS "${PARENT_BUILD_DIR}/makxd-cppConfig.cmake")
        message(STATUS "Found local build at: ${PARENT_BUILD_DIR}")
        set(makxd-cpp_DIR "${PARENT_BUILD_DIR}")
        find_package(makxd-cpp REQUIRED)
    else()
        message(FATAL_ERROR 
            "makxd-cpp library not found!\n"
            "Please either:\n"
            "1. Build and install the library: cd .. && mkdir build && cd build && cmake .. && cmake --build . --config Release && cmake --build . --target install\n"
            "2. Or build the library locally: cd .. && mkdir build && cd build && cmake .. && cmake --build . --config Release\n"
            "Then try building the examples again."
        )
    endif()
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C standard for C API test
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Basic usage example
add_executable(basic_usage basic_usage.cpp)
target_link_libraries(basic_usage PRIVATE makxd::makxd-cpp)

# Full demo application (original main.cpp)
add_executable(demo demo.cpp)
target_link_libraries(demo PRIVATE makxd::makxd-cpp)

# C API test
add_executable(c_api_test c_api_test.c)
target_link_libraries(c_api_test PRIVATE makxd::makxd-cpp)

# Baud rate change test
add_executable(baud_rate_test baud_rate_test.cpp)
target_link_libraries(baud_rate_test PRIVATE makxd::makxd-cpp)

# Keyboard API compile and usage example
add_executable(keyboard_usage keyboard_usage.cpp)
target_link_libraries(keyboard_usage PRIVATE makxd::makxd-cpp)

# Set output directory
set_target_properties(basic_usage demo c_api_test baud_rate_test keyboard_usage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
