#pragma once
#include <string>
#include <iostream>

namespace mpi {

/**
 * Colored terminal output matching bash script style.
 *
 * Provides consistent messaging with "==>" prefix and termcolor support.
 * Colors are automatically disabled when output is not a terminal (e.g., piped).
 */
class Output {
public:
    // Check if stdout is a terminal (for color support)
    static bool isTerminal();

    // Logging functions matching bash script style
    static void info(const std::string& message);      // Blue "==>" prefix
    static void success(const std::string& message);   // Green "==>" prefix
    static void warn(const std::string& message);      // Yellow "==> WARNING:" prefix
    static void error(const std::string& message);     // Red "==> ERROR:" prefix to stderr

    // Print without prefix (for multi-line details)
    static void print(const std::string& message);
    static void printError(const std::string& message);  // To stderr

    // Header with ASCII art (matching bash script)
    static void printHeader();
};

// Convenience functions for less typing
inline void log_info(const std::string& msg) { Output::info(msg); }
inline void log_success(const std::string& msg) { Output::success(msg); }
inline void log_warn(const std::string& msg) { Output::warn(msg); }
inline void log_error(const std::string& msg) { Output::error(msg); }

} // namespace mpi
