cmake_minimum_required(VERSION 3.21)
project({{NAME}} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HarukaEngineFinder.cmake")
find_package(Haruka CONFIG REQUIRED)

haruka_add_project(
    NAME        {{NAME}}
    PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/Source/{{NAME}}GameModule.cpp
)
