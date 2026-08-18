#pragma once
#include <string>
#include <vector>

namespace mpi {

/**
 * ARM64 cross-compilation toolchain management.
 *
 * Checks for aarch64-linux-gnu compilers and ARM64 libraries.
 * Supports automatic installation on Debian/Ubuntu systems.
 */
class Arm64Toolchain {
public:
    /**
     * Check if ARM64 toolchain is available.
     * @param verbose If true, print detailed status to stdout
     * @return true if toolchain is ready, false if missing components
     */
    static bool check(bool verbose = false);

    /**
     * Install ARM64 cross-compilation dependencies.
     * Requires sudo access. Only works on Debian/Ubuntu.
     * @return true on success, false on failure
     */
    static bool installDeps();

    /**
     * Get list of missing compiler tools.
     * @return Vector of missing tool names (e.g., "aarch64-linux-gnu-gcc")
     */
    static std::vector<std::string> getMissingCompilers();

    /**
     * Get list of missing ARM64 libraries.
     * @return Vector of missing library descriptions
     */
    static std::vector<std::string> getMissingLibraries();
};

} // namespace mpi
