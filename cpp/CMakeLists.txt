cmake_minimum_required(VERSION 3.20)
project(makxd-cpp VERSION 1.3.5 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Link-Time Optimization for Release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT MAKXD_IPO_SUPPORTED OUTPUT MAKXD_IPO_OUTPUT)

# Build type options
option(BUILD_SHARED_ONLY "Build only shared library" OFF)
option(BUILD_STATIC_ONLY "Build only static library" OFF)
option(MAKXD_ENABLE_NATIVE_OPTIMIZATIONS "Enable CPU-specific optimizations (-march=native)" OFF)

# Compiler-specific optimizations
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    if(MAKXD_ENABLE_NATIVE_OPTIMIZATIONS)
        string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native")
    endif()
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /Wall")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    # Linux-specific libraries
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED libudev)
endif()

# Library source files
set(LIBRARY_SOURCES
    makxd-cpp/src/makxd.cpp
    makxd-cpp/src/serialport.cpp
    makxd-cpp/src/makxd_c.cpp
)

# Public headers (for distribution)
set(PUBLIC_HEADERS
    makxd-cpp/include/makxd.h
    makxd-cpp/include/makxd_c.h
    makxd-cpp/include/makxd_stream.h
)

# All headers (including internal ones for build)
set(HEADERS
    ${PUBLIC_HEADERS}
    makxd-cpp/include/serialport.h
)

# Create both shared and static libraries by default
set(LIBRARY_TARGETS)

# Build shared library (unless BUILD_STATIC_ONLY is set)
if(NOT BUILD_STATIC_ONLY)
    add_library(${PROJECT_NAME}_shared SHARED ${LIBRARY_SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        PUBLIC_HEADER "${PUBLIC_HEADERS}"
    )
    if(WIN32)
        # Avoid .lib name collision with the static library on Windows.
        set_target_properties(${PROJECT_NAME}_shared PROPERTIES
            ARCHIVE_OUTPUT_NAME "${PROJECT_NAME}_import"
        )
    endif()
    list(APPEND LIBRARY_TARGETS ${PROJECT_NAME}_shared)
endif()

# Build static library (unless BUILD_SHARED_ONLY is set)
if(NOT BUILD_SHARED_ONLY)
    add_library(${PROJECT_NAME}_static STATIC ${LIBRARY_SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME}_static PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        VERSION ${PROJECT_VERSION}
        PUBLIC_HEADER "${PUBLIC_HEADERS}"
    )
    list(APPEND LIBRARY_TARGETS ${PROJECT_NAME}_static)
endif()

# Create alias for backward compatibility - prefer shared library
if(NOT BUILD_STATIC_ONLY)
    add_library(${PROJECT_NAME} ALIAS ${PROJECT_NAME}_shared)
else()
    add_library(${PROJECT_NAME} ALIAS ${PROJECT_NAME}_static)
endif()

# Configure all library targets
foreach(target ${LIBRARY_TARGETS})
    # Include directories for the library
    target_include_directories(${target} 
        PUBLIC 
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/makxd-cpp/include>
            $<INSTALL_INTERFACE:include/makxd>
    )

    # Require C++23 via target_compile_features
    target_compile_features(${target} PUBLIC cxx_std_23)

    # Enable LTO for Release builds
    if(MAKXD_IPO_SUPPORTED)
        set_target_properties(${target} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
        )
    endif()

    # Link libraries
    target_link_libraries(${target} PUBLIC Threads::Threads)

    # Platform-specific linking
    if(WIN32)
        target_link_libraries(${target} PUBLIC setupapi)
        target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0601)
        
        # Export symbols for shared library on Windows
        if(${target} STREQUAL "${PROJECT_NAME}_shared")
            target_compile_definitions(${target} PRIVATE MAKXD_EXPORTS)
            target_compile_definitions(${target} INTERFACE MAKXD_SHARED)
        endif()
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(${target} PUBLIC ${UDEV_LIBRARIES})
        target_include_directories(${target} PUBLIC ${UDEV_INCLUDE_DIRS})
        target_compile_options(${target} PUBLIC ${UDEV_CFLAGS_OTHER})
    endif()

    # Set output directory
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endforeach()

# Install targets
install(TARGETS ${LIBRARY_TARGETS}
    EXPORT makxd-cppTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/makxd
)


# Create and install CMake config files
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/makxd-cppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

export(EXPORT makxd-cppTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/makxd-cppTargets.cmake"
    NAMESPACE makxd::
)

# Generate CMake config file inline
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/makxd-cppConfig.cmake" "
include(CMakeFindDependencyMacro)

# Find required dependencies
find_dependency(Threads REQUIRED)

# Platform-specific dependencies
if(UNIX AND NOT APPLE)
    find_dependency(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED libudev)
endif()

# Include the targets file
include(\"\${CMAKE_CURRENT_LIST_DIR}/makxd-cppTargets.cmake\")

# Create convenient alias - prefer shared library if available, fallback to static
if(TARGET makxd::makxd-cpp_shared)
    if(NOT TARGET makxd::makxd-cpp)
        add_library(makxd::makxd-cpp ALIAS makxd::makxd-cpp_shared)
    endif()
elseif(TARGET makxd::makxd-cpp_static)
    if(NOT TARGET makxd::makxd-cpp)
        add_library(makxd::makxd-cpp ALIAS makxd::makxd-cpp_static)
    endif()
endif()
")

install(EXPORT makxd-cppTargets
    FILE makxd-cppTargets.cmake
    NAMESPACE makxd::
    DESTINATION lib/cmake/makxd-cpp
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/makxd-cppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/makxd-cppConfigVersion.cmake"
    DESTINATION lib/cmake/makxd-cpp
)

# Package configuration
set(CPACK_PACKAGE_NAME "makxd-cpp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-Performance C++ Library for MAKXD Mouse Controllers")
set(CPACK_PACKAGE_VENDOR "terrafirma2021")
set(CPACK_PACKAGE_CONTACT "https://github.com/terrafirma2021/mak-suite")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/terrafirma2021/mak-suite")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Platform-specific package configuration
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    
    # NSIS Configuration for versioned subfolder structure
    set(CPACK_NSIS_DISPLAY_NAME "MAKXD C++ Library")
    set(CPACK_NSIS_PACKAGE_NAME "MAKXD C++ Library")
    set(CPACK_NSIS_CONTACT "https://github.com/terrafirma2021/mak-suite")
    set(CPACK_NSIS_HELP_LINK "https://github.com/terrafirma2021/mak-suite")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/terrafirma2021/mak-suite")
    set(CPACK_NSIS_MODIFY_PATH ON)
    
    # Key setting: Configure install directory structure
    # This creates: C:\Program Files\makxd-cpp\1.1.0
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "makxd-cpp\\\\${PROJECT_VERSION}")
    
    # Optional: Set a more user-friendly program name in Add/Remove Programs
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\makxd-cpp.exe")
    set(CPACK_NSIS_DISPLAY_NAME "MAKXD C++ Library ${PROJECT_VERSION}")
    
    # Create start menu shortcuts (optional)
    set(CPACK_NSIS_MENU_LINKS
        "https://github.com/terrafirma2021/mak-suite" "MAKXD C++ Library Homepage"
    )
    
    # Allow users to choose installation directory
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    
else()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libudev1")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "terrafirma2021")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/terrafirma2021/mak-suite")
    set(CPACK_RPM_PACKAGE_REQUIRES "systemd-devel")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/terrafirma2021/mak-suite")
endif()

include(CPack)
