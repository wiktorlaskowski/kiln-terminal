cmake_minimum_required(VERSION 3.20)
project(kiln-terminal
    VERSION 1.2.0
    DESCRIPTION "A GTK4/libadwaita terminal emulator with multi-shell support"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-deprecated-declarations)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
    gtk4
    libadwaita-1
    vte-2.91-gtk4
)

set(APP_ID "dev.kiln.Terminal")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/config.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/config.hpp"
    @ONLY
)

add_executable(kiln-terminal
    src/main.cpp
    src/application.cpp
    src/window.cpp
    src/terminal_tab.cpp
    src/shell_detector.cpp
)

target_include_directories(kiln-terminal PRIVATE
    src
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(kiln-terminal PRIVATE PkgConfig::DEPS)

install(TARGETS kiln-terminal RUNTIME DESTINATION bin)
install(FILES data/dev.kiln.Terminal.desktop
    DESTINATION share/applications)
