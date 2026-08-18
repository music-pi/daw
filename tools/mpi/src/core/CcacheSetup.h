#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace mpi {

/**
 * Configures ccache for build acceleration.
 *
 * Uses separate cache directories per target to prevent
 * cross-compilation pollution between desktop/headless/pi builds.
 */
class CcacheSetup {
public:
    /**
     * Constructor with target-specific cache directory.
     * @param cacheSubdir Subdirectory name for this target (e.g., "desktop", "pi")
     */
    explicit CcacheSetup(const std::string& cacheSubdir);

    /**
     * Check if ccache is available.
     * @return true if ccache command exists
     */
    static bool isAvailable();

    /**
     * Setup ccache environment variables for this target.
     * Must be called before cmake configure.
     */
    void setupEnvironment();

    /**
     * Get CMake arguments for compiler launchers.
     * @return Vector of cmake args like ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", ...]
     */
    std::vector<std::string> getCMakeArgs() const;

    /**
     * Get cache directory path for logging.
     */
    std::filesystem::path getCacheDir() const { return cacheDir_; }

private:
    std::filesystem::path cacheDir_;
    std::string cacheSubdir_;
};

} // namespace mpi
