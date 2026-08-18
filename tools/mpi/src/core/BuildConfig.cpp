#include "BuildConfig.h"
#include "PathResolver.h"

namespace mpi {

BuildConfig::BuildConfig(BuildTarget target, BuildType type)
    : target_(target), type_(type) {
}

std::string BuildConfig::getDevDesktop() const {
    switch (target_) {
        case BuildTarget::Desktop:
            return "ON";
        case BuildTarget::Headless:
        case BuildTarget::Tools:
        case BuildTarget::Pi:
            return "OFF";
    }
    return "OFF";  // Default fallback
}

std::string BuildConfig::getBuildTools() const {
    switch (target_) {
        case BuildTarget::Tools:
            return "ON";
        case BuildTarget::Headless:
        case BuildTarget::Desktop:
        case BuildTarget::Pi:
            return "OFF";
    }
    return "OFF";  // Default fallback
}

std::string BuildConfig::getBuildType() const {
    switch (type_) {
        case BuildType::Debug:
            return "Debug";
        case BuildType::Release:
            return "Release";
    }
    return "Release";  // Default fallback
}

std::filesystem::path BuildConfig::getBuildDir() const {
    // Pi uses separate build directory to avoid x86_64 conflicts
    if (target_ == BuildTarget::Pi) {
        return PathResolver::buildPiDir();
    }
    return PathResolver::buildDir();
}

std::string BuildConfig::getTargetName() const {
    switch (target_) {
        case BuildTarget::Headless:
            return "headless";
        case BuildTarget::Desktop:
            return "desktop";
        case BuildTarget::Tools:
            return "tools";
        case BuildTarget::Pi:
            return "pi";
    }
    return "unknown";  // Should never reach here
}

std::filesystem::path BuildConfig::getToolchainFile() const {
    // Only Pi target uses cross-compilation toolchain
    if (target_ == BuildTarget::Pi) {
        return PathResolver::getProjectRoot() / "pi-tools/aarch64-toolchain.cmake";
    }
    return "";  // Empty path for native builds
}

std::vector<std::string> BuildConfig::getExtraCMakeArgs() const {
    std::vector<std::string> args;

    // Add toolchain file for Pi cross-compilation
    if (target_ == BuildTarget::Pi) {
        auto toolchainFile = getToolchainFile();
        if (std::filesystem::exists(toolchainFile)) {
            args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + toolchainFile.string());
        }
    }

    return args;
}

} // namespace mpi
