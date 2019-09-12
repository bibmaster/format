cmake_minimum_required(VERSION 3.1.2)
project(fmttest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX)
    set(CXX_COMPILER_OPTIONS ${CXX_COMPILER_OPTIONS} "-Werror -Wextra -Wall")
endif()

# add_definitions(-DFMT_RESPECT_ALIASING=1)

add_subdirectory(src/univang/format)
add_subdirectory(test)
add_subdirectory(benchmark)

