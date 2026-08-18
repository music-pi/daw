#include "ProcessExecutor.h"
#include <memory>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// POSIX headers for waitpid
#include <sys/wait.h>
#include <unistd.h>

namespace mpi {

// ==================== ProcessError ====================

ProcessError::ProcessError(const std::string& cmd, int exitCode, const std::string& output)
    : std::runtime_error("Command failed (exit " + std::to_string(exitCode) + "): " + cmd +
                        "\nOutput: " + output)
    , exitCode_(exitCode)
    , output_(output)
{}

// ==================== RAII popen wrapper ====================

struct PipeDeleter {
    void operator()(FILE* pipe) const {
        if (pipe) {
            pclose(pipe);
        }
    }
};
using PipePtr = std::unique_ptr<FILE, PipeDeleter>;

// ==================== ProcessExecutor ====================

ProcessResult ProcessExecutor::run(const std::string& command) {
    // Open pipe for reading command output
    PipePtr pipe(popen(command.c_str(), "r"));
    if (!pipe) {
        throw ProcessError(command, -1, "Failed to create pipe (popen failed)");
    }

    // Read output
    std::string output;
    std::array<char, 256> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    // Get exit status
    // pclose() returns status from waitpid(), use WEXITSTATUS to extract exit code
    int status = pclose(pipe.release());  // Release ownership before pclose
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    ProcessResult result{exitCode, output, ""};

    if (exitCode != 0) {
        throw ProcessError(command, exitCode, output);
    }

    return result;
}

ProcessResult ProcessExecutor::run(const std::string& command,
                                  const std::filesystem::path& workingDir) {
    // Change to working directory, run command, then restore
    auto originalDir = std::filesystem::current_path();

    try {
        std::filesystem::current_path(workingDir);
        auto result = run(command);
        std::filesystem::current_path(originalDir);
        return result;
    } catch (...) {
        // Restore directory even on exception
        std::filesystem::current_path(originalDir);
        throw;
    }
}

int ProcessExecutor::runStreaming(const std::string& command) {
    // Use system() which inherits stdout/stderr and waits for child
    int status = std::system(command.c_str());

    // Extract exit code from status
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        // Terminated by signal
        return 128 + WTERMSIG(status);
    }

    return -1;  // Unknown error
}

int ProcessExecutor::runStreaming(const std::string& command,
                                 const std::filesystem::path& workingDir) {
    auto originalDir = std::filesystem::current_path();

    try {
        std::filesystem::current_path(workingDir);
        int exitCode = runStreaming(command);
        std::filesystem::current_path(originalDir);
        return exitCode;
    } catch (...) {
        std::filesystem::current_path(originalDir);
        throw;
    }
}

bool ProcessExecutor::commandExists(const std::string& command) {
    // Use 'which' command to check if command exists in PATH
    std::string checkCmd = "which " + command + " > /dev/null 2>&1";
    int result = std::system(checkCmd.c_str());
    return WIFEXITED(result) && WEXITSTATUS(result) == 0;
}

} // namespace mpi
