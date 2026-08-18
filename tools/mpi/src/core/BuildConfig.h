#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace mpi {

/**
 * Build target types.
 */
enum class BuildTarget {
    Headless,  // No GUI (DEV_DESKTOP=OFF)
    Desktop,   // GUI enabled (DEV_DESKTOP=ON)
    Tools,     // Build helper tools (BUILD_TOOLS=ON)
    Pi         // ARM64 cross-compilation for Raspberry Pi
};

/**
 * Build type (Debug or Release).
 */
enum class BuildType {
    Debug,
    Release
};

/**
 * Build configuration - maps build targets to CMake flags.
 *
 * Determines CMake configuration based on target and type:
 * - Headless: DEV_DESKTOP=OFF, build/ directory
 * - Desktop: DEV_DESKTOP=ON, build/ directory
 * - Tools: BUILD_TOOLS=ON, DEV_DESKTOP=OFF, build/ directory
 * - Pi: ARM64 cross-compilation, build-pi/ directory, uses aarch64-toolchain.cmake
 * - Debug: CMAKE_BUILD_TYPE=Debug
 * - Release: CMAKE_BUILD_TYPE=Release
 */
class BuildConfig {
public:
    /**
     * Create build configuration.
     * @param target Build target (headless, desktop, tools)
     * @param type Build type (debug or release)
     */
    BuildConfig(BuildTarget target, BuildType type);

    /**
     * Get DEV_DESKTOP CMake flag value.
     * @return "ON" or "OFF"
     */
    std::string getDevDesktop() const;

    /**
     * Get BUILD_TOOLS CMake flag value.
     * @return "ON" or "OFF"
     */
    std::string getBuildTools() const;

    /**
     * Get CMAKE_BUILD_TYPE value.
     * @return "Debug" or "Release"
     */
    std::string getBuildType() const;

    /**
     * Get build directory path for this configuration.
     * Uses PathResolver to respect BUILD_DIR environment variable.
     * Pi target uses build-pi/ directory to avoid x86_64 conflicts.
     * @return Path to build directory (may not exist)
     */
    std::filesystem::path getBuildDir() const;

    /**
     * Get human-readable target name for logging.
     * @return "headless", "desktop", "tools", or "pi"
     */
    std::string getTargetName() const;

    /**
     * Get toolchain file path (empty if not cross-compiling).
     * @return Path to toolchain file for Pi, empty path otherwise
     */
    std::filesystem::path getToolchainFile() const;

    /**
     * Get additional cmake arguments for this target.
     * @return Vector of extra cmake arguments (e.g., -DCMAKE_TOOLCHAIN_FILE=...)
     */
    std::vector<std::string> getExtraCMakeArgs() const;

private:
    BuildTarget target_;
    BuildType type_;
};

} // namespace mpi
