cmake_minimum_required(VERSION 3.16)
project(system-editor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Core5Compat)

add_executable(system-editor main.cpp)
target_link_libraries(system-editor PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Core5Compat)

install(TARGETS system-editor RUNTIME DESTINATION bin)

# --- CPack (Debian .deb) ---
set(CPACK_PACKAGE_NAME "system-editor")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)   # сам вычислит зависимости (libqt6widgets6 и т.д.)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)