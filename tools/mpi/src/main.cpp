#include <CLI/CLI.hpp>
#include "core/Output.h"
#include "core/PathResolver.h"
#include "core/ProcessExecutor.h"
#include "commands/BuildCommand.h"
#include "commands/CleanCommand.h"
#include "commands/DeployCommand.h"
#include "commands/EmulatorCommand.h"
#include "commands/FlashSdCommand.h"
#include "commands/GenImgCommand.h"
#include "commands/InitCommand.h"
#include "commands/RunCommand.h"
#include "commands/TestCommand.h"

int main(int argc, char** argv) {
    CLI::App app{"MusicPI Build System"};
    app.set_version_flag("-v,--version", "2.0.0");

    // Global options (before subcommand)
    bool verbose = false;
    app.add_flag("--verbose", verbose, "Enable verbose output");

    // Handle no subcommand - show interactive menu or help
    app.require_subcommand(0, 1);  // 0 = optional subcommand

    // Custom failure message
    app.failure_message([](const CLI::App* appPtr, const CLI::Error& e) -> std::string {
        std::string msg = std::string(e.what()) + "\n\nRun 'mpi --help' for usage information.";
        return msg;
    });

    int exitCode = 0;
    try {
        // Add subcommands (inside try — setup may load config which can throw)
        mpi::BuildCommand::setup(app);
        mpi::CleanCommand::setup(app);
        mpi::DeployCommand::setup(app);
        mpi::EmulatorCommand::setup(app);
        mpi::FlashSdCommand::setup(app);
        mpi::GenImgCommand::setup(app);
        mpi::InitCommand::setup(app);
        mpi::RunCommand::setup(app);
        mpi::TestCommand::setup(app);

        CLI11_PARSE(app, argc, argv);

        // If no subcommand given, show menu or help
        if (app.get_subcommands().empty()) {
            mpi::Output::printHeader();
            std::cout << app.help() << "\n";
        }
    } catch (const CLI::ParseError& e) {
        // CLI11 parse errors (invalid arguments, unknown commands, etc.)
        // The failure_message callback formats these nicely
        exitCode = app.exit(e);
    } catch (const mpi::ProcessError& e) {
        // Subprocess execution failures
        mpi::log_error(std::string("Command failed: ") + e.what());
        if (!e.output().empty()) {
            mpi::Output::printError(e.output());
        }
        mpi::Output::printError("");
        mpi::Output::printError("Check the error output above for details.");
        exitCode = e.exitCode() != 0 ? e.exitCode() : 1;
    } catch (const std::runtime_error& e) {
        // Runtime errors (e.g., project root not found)
        mpi::log_error(e.what());
        std::string msg = e.what();

        // Provide specific guidance based on error message
        if (msg.find("project root") != std::string::npos) {
            mpi::Output::printError("");
            mpi::Output::printError("The mpi tool looks for CMakeLists.txt with 'project(maschinepi'.");
            mpi::Output::printError("Make sure you're running from within the maschinepi project directory.");
        }
        exitCode = 1;
    } catch (const std::exception& e) {
        // Other unexpected errors
        mpi::log_error(std::string("Unexpected error: ") + e.what());
        exitCode = 1;
    }

    return exitCode;
}
