#include "EmulatorCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include <filesystem>

namespace mpi {
namespace fs = std::filesystem;

void EmulatorCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("emulator", "Manage the MK3 hardware emulator");
    cmd->allow_extras(true);  // Pass mode + flags through to script
    cmd->require_subcommand(0);  // Don't require subcommand (script handles dispatch)

    cmd->callback([cmd]() {
        auto args = cmd->remaining();
        std::exit(execute(args));
    });
}

int EmulatorCommand::execute(const std::vector<std::string>& args) {
    auto projectRoot = PathResolver::getProjectRoot();
    auto script = projectRoot / "scripts" / "emulator.sh";

    if (!fs::exists(script)) {
        log_error("Script not found: " + script.string());
        return 1;
    }

    if (args.empty()) {
        log_error("Subcommand required: run, qa, or test");
        Output::print("");
        Output::print("  Usage:");
        Output::print("    mpi emulator run [--no-build]          Start emulator + build + run app");
        Output::print("    mpi emulator qa  [--build] [--no-app]  Headless emulator for QA testing");
        Output::print("    mpi emulator test                      Run emulator parity tests");
        Output::print("");
        return 1;
    }

    std::string command = script.string();
    for (const auto& arg : args) {
        command += " " + arg;
    }

    return ProcessExecutor::runStreaming(command);
}

} // namespace mpi
