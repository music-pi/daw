#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Gen-img command - generates Pi images or manual install packages.
 *
 * Delegates to scripts/gen-img.sh which handles loop-mounting,
 * dd, and other operations that are naturally bash.
 */
class GenImgCommand {
public:
    static void setup(CLI::App& app);

private:
    static int execute(const std::vector<std::string>& args);
};

} // namespace mpi
