#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Emulator command - manages the MK3 hardware emulator.
 *
 * Subcommands:
 *   run [--no-build]          Start emulator + build + run app
 *   qa  [--build] [--no-app]  Headless emulator for QA testing
 *   test                       Run emulator parity tests
 *
 * Delegates to scripts/emulator.sh which handles Python venv setup,
 * background process management with traps, and health polling.
 */
class EmulatorCommand {
public:
    static void setup(CLI::App& app);

private:
    static int execute(const std::vector<std::string>& args);
};

} // namespace mpi
