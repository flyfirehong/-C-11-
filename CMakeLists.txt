cmake_minimum_required(VERSION 3.24)
project(threadpool)

set(CMAKE_CXX_STANDARD 11)

add_executable(threadpool main.cpp thread_pool.h)
