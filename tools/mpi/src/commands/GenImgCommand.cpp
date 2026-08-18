#include "GenImgCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include <filesystem>

namespace mpi {
namespace fs = std::filesystem;

void GenImgCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("gen-img", "Generate Pi image or manual install package");
    cmd->allow_extras(true);  // Pass all args through to script

    cmd->callback([cmd]() {
        auto args = cmd->remaining();
        std::exit(execute(args));
    });
}

int GenImgCommand::execute(const std::vector<std::string>& args) {
    auto projectRoot = PathResolver::getProjectRoot();
    auto script = projectRoot / "scripts" / "gen-img.sh";

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
