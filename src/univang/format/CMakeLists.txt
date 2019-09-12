cmake_minimum_required(VERSION 3.1.2)
project(univang.format)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC
    detail/chrono.cpp
    detail/format_double.cpp
    detail/format_double.hpp
    detail/format_double_bignum.cpp
    detail/format_double_fixed.cpp
    detail/format_double_grisu.cpp
    detail/format.cpp
    detail/format_integer.hpp
    detail/format_parsing.hpp
    detail/format_utils.hpp
    buffer.hpp
    chrono.hpp
    format.hpp
    format_context.hpp
    parse_context.hpp
)

add_library(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC ../..)
