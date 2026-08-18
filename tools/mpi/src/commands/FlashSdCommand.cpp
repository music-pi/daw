#include "FlashSdCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include <filesystem>

namespace mpi {
namespace fs = std::filesystem;

void FlashSdCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("flash-sd", "Flash Pi OS image to SD card");
    cmd->allow_extras(true);  // Pass all args through to script

    cmd->callback([cmd]() {
        auto args = cmd->remaining();
        std::exit(execute(args));
    });
}

int FlashSdCommand::execute(const std::vector<std::string>& args) {
    auto projectRoot = PathResolver::getProjectRoot();
    auto script = projectRoot / "scripts" / "flash-sd.sh";

    if (!fs::exists(script)) {
        log_error("Script not found: " + script.string());
        return 1;
    }

    std::string command = script.string();
    for (const auto& arg : args) {
        command += " " + arg;
    }

    return ProcessExecutor::runStreaming(command);
}

} // namespace mpi
