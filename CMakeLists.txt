cmake_minimum_required(VERSION 3.22)

project(coro_runtime VERSION 1.0 LANGUAGES CXX)

add_executable(coro_runtime main.cpp)

set_property(TARGET coro_runtime PROPERTY CXX_STANDARD 23)

