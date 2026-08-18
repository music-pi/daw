#include "InitCommand.h"
#include "../core/Config.h"
#include "../core/Output.h"
#include <filesystem>
#include <fstream>

namespace mpi {
namespace fs = std::filesystem;

void InitCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("init", "Initialize .mpi/config.toml with defaults");

    static bool force = false;
    cmd->add_flag("-f,--force", force, "Overwrite existing config file");

    cmd->callback([]() {
        std::exit(execute(force));
    });
}

int InitCommand::execute(bool force) {
    auto configPath = Config::configPath();

    if (fs::exists(configPath) && !force) {
        log_warn("Config file already exists: " + configPath.string());
        Output::print("");
        Output::print("  Use 'mpi init --force' to overwrite.");
        return 1;
    }

    auto configDir = configPath.parent_path();
    if (!fs::exists(configDir)) {
        try {
            fs::create_directories(configDir);
        } catch (const std::exception& e) {
            log_error("Failed to create directory: " + configDir.string());
            Output::printError("  " + std::string(e.what()));
            return 1;
        }
    }

    std::ofstream out(configPath);
    if (!out) {
        log_error("Failed to create config file: " + configPath.string());
        return 1;
    }
    out << Config::defaultTemplate();
    out.close();

    log_success("Created config file: " + configPath.string());
    Output::print("");
    Output::print("  Edit the config and run 'mpi deploy' to deploy to your Pi.");
    Output::print("");

    return 0;
}

} // namespace mpi
