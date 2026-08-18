cmake_minimum_required(VERSION 3.20)
project(mk3_utilities VERSION 0.1.0 LANGUAGES C)

# Build mk3_cli for Raspberry Pi
# This is the command-line tool for controlling MK3 display

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(FREETYPE2 REQUIRED freetype2)

# Get mk3 library source directory
set(MK3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/mk3)

# Build libmk3 as a static library
add_library(mk3_static STATIC
    ${MK3_DIR}/mk3.c
    ${MK3_DIR}/mk3_display.c
    ${MK3_DIR}/mk3_input.c
    ${MK3_DIR}/mk3_input_map.c
    ${MK3_DIR}/mk3_output.c
    ${MK3_DIR}/mk3_output_map.c
)

target_include_directories(mk3_static PUBLIC
    ${MK3_DIR}
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(mk3_static PUBLIC ${LIBUSB_LIBRARIES})

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mk3_static PRIVATE -Wall -Wextra -O2)
endif()

# mk3_cli - Command-line tool for MK3 display
add_executable(mk3_cli
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/mpi-tools/mk3_cli/mk3_cli.c
)

target_include_directories(mk3_cli PRIVATE
    ${MK3_DIR}
    ${FREETYPE2_INCLUDE_DIRS}
)

target_link_libraries(mk3_cli PRIVATE
    mk3_static
    ${FREETYPE2_LIBRARIES}
    m
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mk3_cli PRIVATE -Wall -Wextra -O2)
endif()

# Install target
install(TARGETS mk3_cli
    RUNTIME DESTINATION bin
)
