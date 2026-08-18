#include "CcacheSetup.h"
#include "PathResolver.h"
#include "ProcessExecutor.h"
#include <cstdlib>

namespace mpi {
namespace fs = std::filesystem;

bool CcacheSetup::isAvailable() {
    return ProcessExecutor::commandExists("ccache");
}

CcacheSetup::CcacheSetup(const std::string& cacheSubdir)
    : cacheSubdir_(cacheSubdir)
{
    // Get base ccache directory from environment or default to .ccache
    const char* envCcacheDir = std::getenv("CCACHE_DIR");
    fs::path baseCacheDir;

    if (envCcacheDir && envCcacheDir[0] != '\0') {
        baseCacheDir = envCcacheDir;
    } else {
        baseCacheDir = PathResolver::getProjectRoot() / ".ccache";
    }

    // Set target-specific subdirectory for cache isolation
    cacheDir_ = baseCacheDir / cacheSubdir;
}

void CcacheSetup::setupEnvironment() {
    if (!isAvailable()) {
        return;
    }

    // Create cache directories
    try {
        fs::create_directories(cacheDir_);
        fs::create_directories(cacheDir_ / "tmp");
    } catch (const std::exception&) {
        // Ignore directory creation errors
        return;
    }

    // Set environment variables for ccache
    auto projectRoot = PathResolver::getProjectRoot();
    setenv("CCACHE_BASEDIR", projectRoot.string().c_str(), 1);
    setenv("CCACHE_DIR", cacheDir_.string().c_str(), 1);
    setenv("CCACHE_TEMPDIR", (cacheDir_ / "tmp").string().c_str(), 1);
    setenv("CCACHE_COMPRESS", "1", 1);
    setenv("CCACHE_MAXSIZE", "10G", 1);

    // Try to configure ccache via command line (ignore errors)
    try {
        ProcessExecutor::run("ccache -o cache_dir=\"" + cacheDir_.string() + "\"");
    } catch (...) {
        // Ignore ccache config errors
    }

    try {
        ProcessExecutor::run("ccache -M 10G");
    } catch (...) {
        // Ignore ccache config errors
    }
}

std::vector<std::string> CcacheSetup::getCMakeArgs() const {
    if (!isAvailable()) {
        return {};
    }

    return {
        "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
        "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    };
}

} // namespace mpi
