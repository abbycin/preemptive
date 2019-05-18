cmake_minimum_required(VERSION 3.10)

project(preempt CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sched make.S main.cc)
target_link_libraries(sched rt)
