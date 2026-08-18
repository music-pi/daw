#pragma once
#include <string>
#include <filesystem>
#include <stdexcept>

namespace mpi {

/**
 * Result of a subprocess execution.
 */
struct ProcessResult {
    int exitCode;
    std::string stdout;
    std::string stderr;

    bool success() const { return exitCode == 0; }
};

/**
 * Exception thrown when a process exits with non-zero code.
 */
class ProcessError : public std::runtime_error {
public:
    ProcessError(const std::string& cmd, int exitCode, const std::string& output);
    int exitCode() const { return exitCode_; }
    const std::string& output() const { return output_; }
private:
    int exitCode_;
    std::string output_;
};

/**
 * RAII wrapper for safe subprocess execution.
 *
 * Ensures proper cleanup of child processes to prevent zombies.
 * Uses popen() for captured execution and system() for streaming.
 */
class ProcessExecutor {
public:
    /**
     * Execute command and capture output.
     * Throws ProcessError on non-zero exit code.
     *
     * @param command Shell command to execute
     * @return ProcessResult with exit code and captured output
     * @throws ProcessError if command exits with non-zero code
     */
    static ProcessResult run(const std::string& command);

    /**
     * Execute command with specific working directory.
     *
     * @param command Shell command to execute
     * @param workingDir Directory to execute command in
     * @return ProcessResult with exit code and captured output
     * @throws ProcessError if command exits with non-zero code
     */
    static ProcessResult run(const std::string& command,
                            const std::filesystem::path& workingDir);

    /**
     * Execute and stream output to stdout/stderr in real-time.
     * Returns exit code directly without throwing.
     *
     * @param command Shell command to execute
     * @return Exit code (0 = success)
     */
    static int runStreaming(const std::string& command);

    /**
     * Execute in specific working directory and stream output.
     *
     * @param command Shell command to execute
     * @param workingDir Directory to execute command in
     * @return Exit code (0 = success)
     */
    static int runStreaming(const std::string& command,
                           const std::filesystem::path& workingDir);

    /**
     * Check if a command exists in PATH.
     *
     * @param command Command name to check (e.g., "cmake", "ninja")
     * @return true if command is available
     */
    static bool commandExists(const std::string& command);
};

} // namespace mpi
