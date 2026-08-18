#pragma once
#include <filesystem>
#include <string>

namespace mpi {

struct DeploySettings {
    std::string host = "maschinepi.local";
    std::string user = "pi";
    std::string remotePath = "/usr/local/bin/maschinepi";
    std::string service = "maschinepi";
};

struct BuildSettings {
    std::string defaultTarget = "headless";
};

/**
 * Project configuration loaded from .mpi/config.toml.
 *
 * Layered defaults: struct initializers → TOML file → CLI flags (at parse time).
 * Missing config file is not an error — all defaults are valid.
 */
class Config {
public:
    /** Load from .mpi/config.toml relative to project root. Defaults if missing. */
    static Config load();

    /** Load from a specific path (for testing). */
    static Config loadFrom(const std::filesystem::path& path);

    static bool exists();
    static std::filesystem::path configPath();

    const DeploySettings& deploy() const { return deploy_; }
    const BuildSettings& build() const { return build_; }
    bool loadedFromFile() const { return loadedFromFile_; }

    /** Default config file content, used by InitCommand. */
    static std::string defaultTemplate();

private:
    DeploySettings deploy_;
    BuildSettings build_;
    bool loadedFromFile_ = false;
};

} // namespace mpi
