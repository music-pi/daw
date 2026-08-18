#include "PathResolver.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <vector>

namespace mpi {

std::optional<std::filesystem::path> PathResolver::cachedRoot_;

std::optional<std::filesystem::path> PathResolver::findProjectRoot() {
    // Return cached result if available
    if (cachedRoot_) {
        return cachedRoot_;
    }

    // Start from current working directory
    auto current = std::filesystem::current_path();

    // Walk up parent directories looking for CMakeLists.txt
    while (true) {
        auto cmakePath = current / "CMakeLists.txt";

        // Check if CMakeLists.txt exists
        if (std::filesystem::exists(cmakePath)) {
            // Read file and check if it contains "project(maschinepi"
            std::ifstream file(cmakePath);
            if (file.is_open()) {
                std::string content;
                std::string line;
                while (std::getline(file, line)) {
                    content += line + "\n";
                    // Early exit if we find the marker
                    if (line.find("project(maschinepi") != std::string::npos) {
                        cachedRoot_ = current;
                        return current;
                    }
                }
            }
        }

        // Move to parent directory
        auto parent = current.parent_path();
        if (parent == current) {
            // Reached root of filesystem, not found
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

std::filesystem::path PathResolver::getProjectRoot() {
    auto root = findProjectRoot();
    if (!root) {
        throw std::runtime_error(
            "Could not find project root. Are you running from within the maschinepi project?"
        );
    }
    return *root;
}

std::filesystem::path PathResolver::buildDir() {
    auto root = getProjectRoot();

    // Check for BUILD_DIR environment variable
    const char* envBuildDir = std::getenv("BUILD_DIR");
    if (envBuildDir && envBuildDir[0] != '\0') {
        return root / envBuildDir;
    }

    // Default to "build"
    return root / "build";
}

std::filesystem::path PathResolver::buildPiDir() {
    auto root = getProjectRoot();

    // Check for BUILD_PI_DIR environment variable
    const char* envBuildPiDir = std::getenv("BUILD_PI_DIR");
    if (envBuildPiDir && envBuildPiDir[0] != '\0') {
        return root / envBuildPiDir;
    }

    // Default to "build-pi"
    return root / "build-pi";
}

std::optional<std::filesystem::path> PathResolver::findBinary() {
    auto buildPath = buildDir();

    // Search paths matching bash script logic from ./mpi
    std::vector<std::filesystem::path> searchPaths = {
        buildPath / "bin" / "maschinepi",
        buildPath / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Release" / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Debug" / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Release" / "Standalone" / "maschinepi",
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
            // Check if file is executable (on Unix-like systems)
            auto perms = std::filesystem::status(path).permissions();
            if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                return path;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> PathResolver::findPiBinary() {
    auto buildPath = buildPiDir();

    // Search paths matching bash script logic from ./mpi (same as findBinary)
    std::vector<std::filesystem::path> searchPaths = {
        buildPath / "bin" / "maschinepi",
        buildPath / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Release" / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Debug" / "maschinepi",
        buildPath / "maschinepi_artefacts" / "Release" / "Standalone" / "maschinepi",
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
            // Note: We don't check executable permission for cross-compiled binaries
            // since ARM64 binaries may not have x86_64 executable flags set correctly
            return path;
        }
    }

    return std::nullopt;
}

} // namespace mpi
