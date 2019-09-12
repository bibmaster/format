cmake_minimum_required(VERSION 3.1.2)
project(format-benchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} ./format-benchmark.cpp)
target_link_libraries(${PROJECT_NAME} univang.format CONAN_PKG::google-benchmark CONAN_PKG::fmt Threads::Threads)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} shlwapi.lib)
endif()

