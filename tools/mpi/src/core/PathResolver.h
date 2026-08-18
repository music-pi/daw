#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace mpi {

/**
 * Utility class for resolving project paths.
 *
 * Finds the maschinepi project root by walking up from the current directory
 * and searching for CMakeLists.txt containing "project(maschinepi".
 *
 * This allows the mpi CLI to work from any subdirectory within the project.
 */
class PathResolver {
public:
    /**
     * Find project root by searching upward for CMakeLists.txt.
     * @return Path to project root, or nullopt if not found (not in a project)
     */
    static std::optional<std::filesystem::path> findProjectRoot();

    /**
     * Get project root or throw if not found.
     * @return Path to project root
     * @throws std::runtime_error if not in a maschinepi project directory
     */
    static std::filesystem::path getProjectRoot();

    /**
     * Get build directory path.
     * Respects BUILD_DIR environment variable, defaults to "build".
     * @return Path to build directory (may not exist)
     */
    static std::filesystem::path buildDir();

    /**
     * Get Raspberry Pi build directory path.
     * Respects BUILD_PI_DIR environment variable, defaults to "build-pi".
     * @return Path to Pi build directory (may not exist)
     */
    static std::filesystem::path buildPiDir();

    /**
     * Find the maschinepi binary in the build directory.
     * Searches common locations from JUCE builds.
     * @return Path to binary, or nullopt if not found
     */
    static std::optional<std::filesystem::path> findBinary();

    /**
     * Find the maschinepi binary in the Pi build directory.
     * Searches common locations from JUCE ARM64 builds.
     * @return Path to ARM64 binary, or nullopt if not found
     */
    static std::optional<std::filesystem::path> findPiBinary();

private:
    static std::optional<std::filesystem::path> cachedRoot_;
};

} // namespace mpi
