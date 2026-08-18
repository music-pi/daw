#include "RunCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <vector>

namespace mpi {

void RunCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("run", "Run the maschinepi application");

    // Flags
    static bool useGdb = false;
    cmd->add_flag("--gdb", useGdb, "Run with GDB debugger attached");

    cmd->callback([&]() {
        std::exit(execute(useGdb));
    });
}

int RunCommand::execute(bool useGdb) {
    // Find the binary
    auto binaryPath = PathResolver::findBinary();

    if (!binaryPath) {
        log_error("Could not find maschinepi binary");
        Output::print("");
        Output::print("  Searched locations:");
        Output::print("    - build/bin/maschinepi");
        Output::print("    - build/maschinepi");
        Output::print("    - build/maschinepi_artefacts/Release/maschinepi");
        Output::print("    - build/maschinepi_artefacts/Debug/maschinepi");
        Output::print("");
        Output::print("  Have you run 'mpi build' first?");
        Output::print("");
        return 2;
    }

    log_info("Running " + binaryPath->string());

    // Check for display on Linux
    const char* display = std::getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0') {
        log_warn("$DISPLAY is empty. No GUI will appear.");
    }

    // Build argv vector
    std::vector<const char*> args;

    // Check for pw-jack (PipeWire/JACK bridge)
    bool hasPwJack = ProcessExecutor::commandExists("pw-jack");

    // Add GDB prefix if requested
    if (useGdb) {
        log_info("Running with GDB debugger");
        args.push_back("gdb");
        args.push_back("--args");
    }

    // Add pw-jack wrapper if available (and not debugging - gdb + pw-jack can be tricky)
    if (hasPwJack && !useGdb) {
        args.push_back("pw-jack");
    }

    // Add the binary path
    std::string binaryStr = binaryPath->string();
    args.push_back(binaryStr.c_str());

    // Null terminate
    args.push_back(nullptr);

    // Log what we're about to execute
    std::string cmdLine;
    for (size_t i = 0; args[i] != nullptr; ++i) {
        if (i > 0) cmdLine += " ";
        cmdLine += args[i];
    }
    Output::print("  Command: " + cmdLine);
    Output::print("");

    // Flush stdout before exec to ensure all messages appear
    std::cout.flush();

    // Replace this process with the target binary
    // execvp will search PATH for gdb/pw-jack if they're the first arg
    execvp(args[0], const_cast<char* const*>(args.data()));

    // If we get here, exec failed
    log_error("Failed to execute: " + std::string(std::strerror(errno)));
    return 127;
}

} // namespace mpi
