#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Build command - builds the project for desktop, pi, or other targets.
 *
 * Supports native builds (headless, desktop) and ARM64 cross-compilation (pi).
 * Pi builds include toolchain checking and automatic dependency installation.
 */
class BuildCommand {
public:
    /**
     * Setup the build subcommand with CLI11.
     * @param app Parent CLI::App to add the subcommand to
     */
    static void setup(CLI::App& app);

private:
    static int execute(const std::string& target, bool debug, bool release,
                      bool checkToolchain, bool installDeps, bool emulatorMode);
};

} // namespace mpi
