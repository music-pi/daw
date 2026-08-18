#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

class InitCommand {
public:
    static void setup(CLI::App& app);

private:
    static int execute(bool force);
};

} // namespace mpi
