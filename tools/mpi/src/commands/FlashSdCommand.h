#pragma once
#include <CLI/CLI.hpp>

namespace mpi {

/**
 * Flash-sd command - writes Pi OS image to SD card with configuration.
 *
 * Delegates to scripts/flash-sd.sh which handles interactive prompts,
 * dd, partition mounting, and WiFi/user configuration.
 */
class FlashSdCommand {
public:
    static void setup(CLI::App& app);

private:
    static int execute(const std::vector<std::string>& args);
};

} // namespace mpi
