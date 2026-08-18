#include "CleanCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace mpi {
namespace fs = std::filesystem;

void CleanCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("clean", "Remove build directories and artifacts");

    cmd->callback([]() {
        std::exit(execute());
    });
}

int CleanCommand::execute() {
    auto projectRoot = PathResolver::getProjectRoot();
    auto buildDir = PathResolver::buildDir();
    auto buildPiDir = PathResolver::buildPiDir();
    auto ccacheDir = projectRoot / ".ccache";

    log_info("Cleaning build artifacts...");
    Output::print("");

    bool removedSomething = false;

    // Remove build/ directory
    if (fs::exists(buildDir)) {
        std::error_code ec;
        fs::remove_all(buildDir, ec);
        if (ec) {
            log_error("Failed to remove " + buildDir.filename().string() + "/: " + ec.message());
        } else {
            log_success("Removed " + buildDir.filename().string() + "/");
            removedSomething = true;
        }
    }

    // Remove build-pi/ directory
    if (fs::exists(buildPiDir)) {
        std::error_code ec;
        fs::remove_all(buildPiDir, ec);
        if (ec) {
            log_error("Failed to remove " + buildPiDir.filename().string() + "/: " + ec.message());
        } else {
            log_success("Removed " + buildPiDir.filename().string() + "/");
            removedSomething = true;
        }
    }

    // Handle ccache directory with interactive prompt
    if (fs::exists(ccacheDir)) {
        bool shouldRemove = false;

        // Check if stdin is a terminal (interactive mode)
        if (isatty(STDIN_FILENO)) {
            std::cout << "Also clear ccache? [y/N] ";
            std::cout.flush();

            std::string response;
            std::getline(std::cin, response);

            if (!response.empty() && (response[0] == 'y' || response[0] == 'Y')) {
                shouldRemove = true;
            }
        }
        // Non-interactive mode: skip ccache removal (default to no)

        if (shouldRemove) {
            std::error_code ec;
            fs::remove_all(ccacheDir, ec);
            if (ec) {
                log_error("Failed to remove .ccache/: " + ec.message());
            } else {
                log_success("Removed .ccache/");
                removedSomething = true;
            }
        }
    }

    Output::print("");

    if (!removedSomething) {
        log_info("Nothing to clean");
    } else {
        log_success("Clean complete!");
    }

    Output::print("");
    return 0;
}

} // namespace mpi
