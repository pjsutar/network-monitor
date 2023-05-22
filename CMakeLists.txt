cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

project(network-monitor)

# Add the local CMake modules folder to the CMake search path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Dependencies
# If dependencies are managed with conan, we prepend the current build folder
# to the CMake module path, where CMake looks for dependencies.
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/conaninfo.txt)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR}")
endif()
find_package(Boost 1.74 REQUIRED COMPONENTS system)

# On Windows, we define a preprocessor symbol with the OS version to prevent
# warnings from the Boost.Asio header files.
if(WIN32)
    include(GetWinVer REQUIRED)
    get_win_ver(WINDOWS_VERSION)
endif()

set(SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_executable(network-monitor ${SOURCES})
target_compile_features(network-monitor
    PRIVATE
        cxx_std_11
)
target_compile_definitions(network-monitor
    PRIVATE
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=${WINDOWS_VERSION}>
)
target_link_libraries(network-monitor
    PRIVATE
        Boost::Boost
)