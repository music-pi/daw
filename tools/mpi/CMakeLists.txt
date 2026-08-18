cmake_minimum_required(VERSION 3.20)
project(mpi-cli VERSION 1.0.0 LANGUAGES CXX)

# C++20 standard requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set output directory to project root bin/ (so ./bin/mpi works)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# ======================== Dependencies ========================

include(FetchContent)

# CLI11 - Command line parser (header-only, v2.6.1)
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.6.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cli11)

# termcolor - Terminal color output (single header)
FetchContent_Declare(
  termcolor
  GIT_REPOSITORY https://github.com/ikalnytskyi/termcolor.git
  GIT_TAG v2.1.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(termcolor)

# toml++ - TOML config file parser (header-only, v3.4.0)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# ======================== Executable ========================

add_executable(mpi
  src/main.cpp
  src/core/PathResolver.cpp
  src/core/ProcessExecutor.cpp
  src/core/Output.cpp
  src/core/BuildConfig.cpp
  src/core/GeneratorDetector.cpp
  src/core/CcacheSetup.cpp
  src/core/Arm64Toolchain.cpp
  src/core/ElfChecker.cpp
  src/commands/BuildCommand.cpp
  src/commands/CleanCommand.cpp
  src/commands/DeployCommand.cpp
  src/commands/EmulatorCommand.cpp
  src/commands/FlashSdCommand.cpp
  src/commands/GenImgCommand.cpp
  src/commands/InitCommand.cpp
  src/commands/RunCommand.cpp
  src/commands/TestCommand.cpp
  src/core/Config.cpp
)

target_include_directories(mpi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(mpi PRIVATE
  CLI11::CLI11
  tomlplusplus::tomlplusplus
)

# termcolor is header-only, just add include path
target_include_directories(mpi PRIVATE
  ${termcolor_SOURCE_DIR}/include
)

# Warnings for our code
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mpi PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
  )
endif()

message(STATUS "mpi CLI configured:")
message(STATUS "  Output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mpi")
