#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Clean command - removes build artifacts and optionally ccache.
 *
 * Features:
 * - Removes build/ directory (desktop builds)
 * - Removes build-pi/ directory (Pi cross-compilation builds)
 * - Prompts before removing .ccache/ directory (interactive mode only)
 * - Skips ccache removal in non-interactive mode (piped input)
 */
class CleanCommand {
public:
    /**
     * Setup the clean subcommand with CLI11.
     * @param app Parent CLI::App to add the subcommand to
     */
    static void setup(CLI::App& app);

private:
    static int execute();
};

} // namespace mpi
