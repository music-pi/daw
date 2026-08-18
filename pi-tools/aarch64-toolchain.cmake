# CMake toolchain file for cross-compiling to ARM64 (aarch64)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=pi-tools/aarch64-toolchain.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler settings
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Find programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Find libraries and packages in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config configuration for cross-compilation
# Use native pkg-config but point it to ARM64 libraries (multiarch)
set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config")

# For Debian/Ubuntu multiarch: ARM64 libs are in /usr/lib/aarch64-linux-gnu
set(ENV{PKG_CONFIG_PATH} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")

# Set library search paths for the linker
set(CMAKE_LIBRARY_PATH "/usr/lib/aarch64-linux-gnu")
set(CMAKE_INCLUDE_PATH "/usr/include/aarch64-linux-gnu")

# Add ARM64 library path to find_library searches
set(CMAKE_FIND_ROOT_PATH "/usr/aarch64-linux-gnu" "/usr/lib/aarch64-linux-gnu")

# GCC ARM64 compatibility: __wfe() is a Clang builtin, GCC cross-compiler lacks it
# Force-include a compatibility header that provides __wfe() via inline asm
set(CMAKE_CXX_FLAGS_INIT "-include ${CMAKE_CURRENT_LIST_DIR}/arm64-compat.h")
set(CMAKE_C_FLAGS_INIT "-include ${CMAKE_CURRENT_LIST_DIR}/arm64-compat.h")

# Set sysroot if needed (optional, for custom sysroot)
# set(CMAKE_SYSROOT /path/to/sysroot)
