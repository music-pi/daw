#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Run command - launches the maschinepi application.
 *
 * Features:
 * - Locates the binary via PathResolver::findBinary()
 * - Optional --gdb flag for debugger attachment
 * - Automatic pw-jack wrapper when PipeWire is available
 * - Uses execvp() for clean process replacement
 */
class RunCommand {
public:
    /**
     * Setup the run subcommand with CLI11.
     * @param app Parent CLI::App to add the subcommand to
     */
    static void setup(CLI::App& app);

private:
    static int execute(bool useGdb);
};

} // namespace mpi
