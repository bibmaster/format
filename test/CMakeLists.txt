cmake_minimum_required(VERSION 3.1.2)
project(format.test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_executable(${PROJECT_NAME} test-main.cpp)

target_link_libraries(${PROJECT_NAME} univang.format CONAN_PKG::gtest)
