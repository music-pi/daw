#include "Config.h"
#include "Output.h"
#include "PathResolver.h"
#include <toml++/toml.hpp>
#include <sstream>

namespace mpi {

Config Config::load() {
    auto path = configPath();
    if (!std::filesystem::exists(path))
        return Config{};
    return loadFrom(path);
}

Config Config::loadFrom(const std::filesystem::path& path) {
    Config cfg;

    if (!std::filesystem::exists(path))
        return cfg;

    try {
        auto tbl = toml::parse_file(path.string());
        cfg.loadedFromFile_ = true;

        if (auto deploy = tbl["deploy"].as_table()) {
            if (auto v = (*deploy)["host"].value<std::string>())
                cfg.deploy_.host = *v;
            if (auto v = (*deploy)["user"].value<std::string>())
                cfg.deploy_.user = *v;
            if (auto v = (*deploy)["remote_path"].value<std::string>())
                cfg.deploy_.remotePath = *v;
            if (auto v = (*deploy)["service"].value<std::string>())
                cfg.deploy_.service = *v;
        }

        if (auto build = tbl["build"].as_table()) {
            if (auto v = (*build)["default_target"].value<std::string>())
                cfg.build_.defaultTarget = *v;
        }

    } catch (const toml::parse_error& err) {
        std::ostringstream msg;
        msg << "Config parse error in " << path.string() << ":\n"
            << "  " << err.description() << "\n"
            << "  (line " << err.source().begin.line
            << ", column " << err.source().begin.column << ")";
        log_error(msg.str());
        throw std::runtime_error("Invalid config file: " + path.string());
    }

    return cfg;
}

std::filesystem::path Config::configPath() {
    return PathResolver::getProjectRoot() / ".mpi" / "config.toml";
}

bool Config::exists() {
    return std::filesystem::exists(configPath());
}

std::string Config::defaultTemplate() {
    return R"(# MusicPI project configuration
# Edit these values for your setup, then run mpi deploy
# CLI flags (e.g., --host) override these values

[deploy]
host = "maschinepi.local"
user = "pi"
remote_path = "/usr/local/bin/maschinepi"
service = "maschinepi"

[build]
default_target = "headless"
)";
}

} // namespace mpi
