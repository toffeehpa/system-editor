cmake_minimum_required(VERSION 3.16)
project(system-editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Core5Compat)

add_executable(system-editor main.cpp)
target_link_libraries(system-editor PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Core5Compat)
