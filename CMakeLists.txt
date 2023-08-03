cmake_minimum_required(VERSION 3.1)

project(MIMUW_SIK_BOMBERMAN)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wconversion -O2 -Werror")

find_package(Boost 1.74.0 COMPONENTS program_options system)
find_package(Threads)

include_directories(include)

add_executable(robots-client robots-client.cpp)
target_link_libraries(robots-client Boost::program_options Boost::system Threads::Threads)

add_executable(robots-server robots-server.cpp)
target_link_libraries(robots-server Boost::program_options Boost::system Threads::Threads)
