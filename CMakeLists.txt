cmake_minimum_required(VERSION 3.20)
project(maschinepi VERSION 0.1.0 LANGUAGES C CXX)

enable_testing()

# ------------------------ Policies & toolchain -------------------------------
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW) # honor parent-set option() cache values
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------ Compile cache (ccache) -----------------------------
# The `./mpi build` wrapper also passes -DCMAKE_CXX_COMPILER_LAUNCHER=ccache,
# but configure a fallback here so plain `cmake ..` invocations also benefit.
if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache: using ${CCACHE_PROGRAM}")
  endif()
endif()

# Standardize output directories for all targets
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ------------------------ Options -------------------------------------------
option(DEV_DESKTOP "Enable desktop GUI window (JUCE headless off)" ON)
option(ENABLE_TRACKTION "Build with Tracktion Engine" ON)
option(BUILD_TOOLS "Build mpi-tools helper binaries" OFF)
option(ENABLE_COVERAGE "Enable code coverage instrumentation for tests" OFF)
# EMULATOR_MODE is deprecated — emulator support is now built-in at runtime.
# Both USB hardware and emulator can run simultaneously.

# ------------------------ JUCE lean flags -----------------------------------
# Slank JUCE build – men lad GUI være tændt i DEV_DESKTOP
add_compile_definitions(
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
)

if(DEV_DESKTOP)
  add_compile_definitions(JUCE_HEADLESS=0)
else()
  add_compile_definitions(JUCE_HEADLESS=1)
endif()

# JUCE plugin hosting flags are target-properties on juce_add_gui_app /
# juce_add_console_app, not global cache vars — set on each target below.

# ------------------------ Tracktion Engine (submodule) -----------------------
# Forventet layout: external/tracktion_engine
set(TE_DIR ${CMAKE_SOURCE_DIR}/external/tracktion_engine)
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)

if(ENABLE_TRACKTION)
  if (EXISTS "${TE_DIR}/CMakeLists.txt")
    add_subdirectory(${TE_DIR})  # <- Denne bringer JUCE ind som subdir og eksporterer juce:: targets
  else()
    message(FATAL_ERROR
      "Tracktion Engine not found at ${TE_DIR}. "
      "Did you add it as a git submodule and run 'git submodule update --init --recursive'?")
  endif()
endif()

# ------------------------ Warnings ------------------------------------------
# Note: Warnings are applied per-target below to avoid affecting external code

# ------------------------ Sources (auto-discovered via glob) ------------------------------
# Automatically discover all source files under src/
file(GLOB_RECURSE SRC_APP
  src/app/*.cpp
  src/app/*.h
)

file(GLOB_RECURSE SRC_ENGINE
  src/engine/*.cpp
  src/engine/*.h
)

file(GLOB_RECURSE SRC_CONTROL
  src/control/*.cpp
  src/control/*.h
)

file(GLOB_RECURSE SRC_INPUT
  src/input/*.cpp
  src/input/*.h
)

file(GLOB_RECURSE SRC_DEVICES
  src/devices/*.cpp
  src/devices/*.h
)

file(GLOB_RECURSE SRC_UI
  src/ui/*.cpp
  src/ui/*.h
)

file(GLOB_RECURSE SRC_SAMPLES
  src/samples/*.cpp
  src/samples/*.h
)

# Embedded UI assets
juce_add_binary_data(maschinepi_ui_assets
  HEADER_NAME UiBinaryData.h
  NAMESPACE UiBinaryData
  SOURCES
    src/ui/assets/bg-default.png
    src/ui/assets/bg-default2.png
)

# ------------------------ App target (JUCE GUI) ------------------------------
# JUCE er allerede tilføjet via Tracktion Engine, så vi kan bruge juce_add_gui_app
juce_add_gui_app(maschinepi
  VERSION      ${PROJECT_VERSION}
  PRODUCT_NAME "maschinepi"
  COMPANY_NAME "MaschinePI"
  BUNDLE_ID    "com.maschinepi.app"
  ICON_BIG     ""          # valgfrit
  MICROPHONE_PERMISSION_ENABLED TRUE  # macOS/iOS, har ingen effekt på desktop Linux/Win
)

# JUCE's juce_add_gui_app only auto-generates JUCE_PLUGINHOST_AU=1 from the
# PLUGINHOST_AU target property; VST3 / LV2 require explicit defines (see
# JUCEUtils.cmake ~L2158). Setting them via target_compile_definitions makes
# juce_audio_processors compile in VST3PluginFormat and LV2PluginFormat, and
# juce::addDefaultFormatsToManager then picks them up.
target_compile_definitions(maschinepi PUBLIC
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_LV2=1
)

target_sources(maschinepi PRIVATE
  ${SRC_APP}
  ${SRC_ENGINE}
  ${SRC_CONTROL}
  ${SRC_INPUT}
  ${SRC_DEVICES}
  ${SRC_UI}
  ${SRC_SAMPLES}
)

target_include_directories(maschinepi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/external/mk3
)

# Warnings for our code only (using regex to match our source paths)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(${SRC_APP} ${SRC_ENGINE} ${SRC_CONTROL} ${SRC_INPUT} ${SRC_DEVICES} ${SRC_UI} ${SRC_SAMPLES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wno-unused-parameter"
  )
endif()

# ------------------------ Test sources (auto-discovered via glob) ------------
file(GLOB_RECURSE TEST_SRC_INPUT CONFIGURE_DEPENDS src/input/*.cpp)
file(GLOB_RECURSE TEST_SRC_ENGINE CONFIGURE_DEPENDS src/engine/*.cpp)
file(GLOB_RECURSE TEST_SRC_CONTROL CONFIGURE_DEPENDS src/control/*.cpp)
file(GLOB_RECURSE TEST_SRC_DEVICES CONFIGURE_DEPENDS src/devices/*.cpp)
file(GLOB_RECURSE TEST_SRC_TESTS CONFIGURE_DEPENDS tests/*.cpp)
file(GLOB_RECURSE TEST_SRC_UI_COMPONENTS CONFIGURE_DEPENDS src/ui/components/*.cpp)
file(GLOB_RECURSE TEST_SRC_UI_SCREEN CONFIGURE_DEPENDS src/ui/screen/*.cpp)
file(GLOB_RECURSE TEST_SRC_UI_ASSETS CONFIGURE_DEPENDS src/ui/assets/*.cpp)
file(GLOB_RECURSE TEST_SRC_UI_HW CONFIGURE_DEPENDS src/ui/hw/*.cpp)
file(GLOB_RECURSE TEST_SRC_UI_WIDGET CONFIGURE_DEPENDS src/ui/widget/*.cpp)
file(GLOB_RECURSE TEST_SRC_SAMPLES CONFIGURE_DEPENDS src/samples/*.cpp)

juce_add_console_app(maschinepi_tests
  PRODUCT_NAME "MaschinePI Tests"
)

target_compile_definitions(maschinepi_tests PUBLIC
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_LV2=1
)

target_sources(maschinepi_tests PRIVATE
  ${TEST_SRC_INPUT}
  ${TEST_SRC_ENGINE}
  ${TEST_SRC_CONTROL}
  ${TEST_SRC_DEVICES}
  ${TEST_SRC_TESTS}
  ${TEST_SRC_UI_COMPONENTS}
  ${TEST_SRC_UI_SCREEN}
  ${TEST_SRC_UI_ASSETS}
  ${TEST_SRC_UI_HW}
  ${TEST_SRC_UI_WIDGET}
  ${TEST_SRC_SAMPLES}
  src/app/DataPaths.cpp
  # UiRefresh helper (needed by widget repaint wiring and UiRefreshTests)
  src/ui/UiRefresh.cpp
  # AudioEditor operations (used by AudioEditorWidget)
  src/ui/views/operations/SampleRangeOperation.cpp
  src/ui/views/operations/NormalizeOperation.cpp
  src/ui/views/operations/SliceOperation.cpp
)

target_include_directories(maschinepi_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/external/mk3
  ${CMAKE_CURRENT_SOURCE_DIR}/external/tracktion_engine/modules
)

target_compile_definitions(maschinepi_tests PRIVATE
  JUCE_MODAL_LOOPS_PERMITTED=0
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
  MASCHINEPI_TESTS=1
)

# Warnings for our test code only
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(
    ${TEST_SRC_INPUT} ${TEST_SRC_ENGINE} ${TEST_SRC_CONTROL} ${TEST_SRC_DEVICES}
    ${TEST_SRC_TESTS} ${TEST_SRC_UI_COMPONENTS} ${TEST_SRC_UI_SCREEN} ${TEST_SRC_UI_ASSETS} ${TEST_SRC_UI_HW} ${TEST_SRC_UI_WIDGET}
    src/app/DataPaths.cpp
    src/ui/views/operations/SampleRangeOperation.cpp src/ui/views/operations/NormalizeOperation.cpp
    src/ui/views/operations/SliceOperation.cpp
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wno-unused-parameter"
  )
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

target_link_libraries(maschinepi_tests
  PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_audio_formats
    maschinepi_ui_assets
    tracktion_engine
    mk3
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(maschinepi_tests
  TEST_FILTER
    "*-RealTimePerfTest.*:RealTimePerf/BufferSizeSweepTest.*:TracktionStrategies/ThreadPoolStrategyTest.*"
  DISCOVERY_MODE PRE_TEST
)

# Real-time performance measurements must not compete with one another. CTest
# otherwise starts the buffer-size and thread-pool stress cases concurrently,
# making xrun assertions measure test-runner contention instead of the engine.
gtest_discover_tests(maschinepi_tests
  TEST_FILTER
    "RealTimePerfTest.*:RealTimePerf/BufferSizeSweepTest.*:TracktionStrategies/ThreadPoolStrategyTest.*"
  PROPERTIES
    RESOURCE_LOCK maschinepi_realtime_perf
  DISCOVERY_MODE PRE_TEST
)

target_include_directories(maschinepi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ------------------------ Linking -------------------------------------------
# JUCE GUI + audio devices (til dev test)
target_link_libraries(maschinepi
  PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_basics
    maschinepi_ui_assets
)

# Tracktion Engine (hvis slået til)
if(ENABLE_TRACKTION)
  target_link_libraries(maschinepi PRIVATE tracktion_engine)
  target_compile_definitions(maschinepi PRIVATE ENABLE_TRACKTION=1)
else()
  target_compile_definitions(maschinepi PRIVATE ENABLE_TRACKTION=0)
endif()

# ------------------------ MK3 hardware driver --------------------------------
# USB driver is always linked. Emulator support is built-in via EmulatorController
# (connects to the Python emulator at runtime — no compile-time switching needed).
add_subdirectory(external/mk3)
target_link_libraries(maschinepi PRIVATE mk3)

# Tests always use the real mk3 library
if(TARGET mk3)
  add_dependencies(maschinepi_tests mk3)
endif()

# ------------------------ mpi-tools -------------------------------------------
if(BUILD_TOOLS)
  # Set output directory for tools
  set(TOOLS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/mpi-tools)
  file(MAKE_DIRECTORY ${TOOLS_OUTPUT_DIR})
  
  # Set RUNTIME_OUTPUT_DIRECTORY for all tool executables
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_TOOLS ${TOOLS_OUTPUT_DIR})
  
  # audio_cli requires audio library headers that may not be available
  # Check if audio/context.h exists (or make it optional)
  set(AUDIO_CLI_ENABLED OFF)
  if(EXISTS "${CMAKE_SOURCE_DIR}/external/mpi-tools/audio_cli/audio_cli.c")
    # Try to build it - will fail at compile time if headers missing
    # For now, skip audio_cli as it has missing dependencies
    # add_subdirectory(external/mpi-tools/audio_cli)
    message(STATUS "  audio_cli: skipped (missing dependencies)")
  endif()
  
  add_subdirectory(external/mpi-tools/mk3_cli)
  add_subdirectory(external/mpi-tools/mk3_test)
  
  # Set output directory for each tool
  set(TOOLS_TARGETS mk3_cli mk3_test)
  if(AUDIO_CLI_ENABLED)
    list(APPEND TOOLS_TARGETS audio_cli)
  endif()
  set_target_properties(${TOOLS_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TOOLS_OUTPUT_DIR}
  )
endif()

# App-specifikke defs (modal loops typisk ikke ønsket i embedded)
target_compile_definitions(maschinepi PRIVATE JUCE_MODAL_LOOPS_PERMITTED=0)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(maschinepi_tests PRIVATE --coverage -O0)
  target_link_options(maschinepi_tests PRIVATE --coverage)
  target_compile_options(maschinepi PRIVATE --coverage -O0)
  target_link_options(maschinepi PRIVATE --coverage)
endif()

# ------------------------ Platform specifics --------------------------------
if(APPLE)
  # Tilføj entitlements/sandbox hvis nødvendigt
elseif(WIN32)
  # Intet specielt pt.
else()
  # Linux: sørg for ALSA/X11 pakker via pkg-config i build-miljøet
endif()

# ------------------------ Install (valgfrit) ---------------------------------
include(GNUInstallDirs)
install(TARGETS maschinepi
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ------------------------ CLI tool (separate build) -------------------------
add_subdirectory(tools/mpi EXCLUDE_FROM_ALL)

# ------------------------ Build summary --------------------------------------
message(STATUS "")
message(STATUS "---------------------------------------------------")
message(STATUS "  Build config")
message(STATUS "  DEV_DESKTOP       : ${DEV_DESKTOP}")
message(STATUS "  ENABLE_TRACKTION  : ${ENABLE_TRACKTION}")
message(STATUS "  BUILD_TOOLS       : ${BUILD_TOOLS}")
message(STATUS "  Emulator          : built-in (runtime)")
message(STATUS "  CMAKE_BUILD_TYPE  : ${CMAKE_BUILD_TYPE}")
message(STATUS "---------------------------------------------------")
message(STATUS "")
