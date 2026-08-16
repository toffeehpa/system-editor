cmake_minimum_required(VERSION 3.16)
project(systemeditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

add_executable(systemeditor main.cpp)
target_link_libraries(systemeditor PRIVATE Qt6::Widgets Qt6::PrintSupport)
