#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Test command - runs project tests via ctest.
 *
 * Features:
 * - Parallel test execution using all CPU cores
 * - Optional --coverage flag for code coverage
 * - Rebuilds with coverage enabled if needed
 * - Generates gcovr coverage report when available
 */
class TestCommand {
public:
    /**
     * Setup the test subcommand with CLI11.
     * @param app Parent CLI::App to add the subcommand to
     */
    static void setup(CLI::App& app);

private:
    static int execute(bool withCoverage);
};

} // namespace mpi
