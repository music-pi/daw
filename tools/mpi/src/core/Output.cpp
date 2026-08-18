#include "Output.h"
#include <cstdint>  // Fix termcolor v2.1.0 missing include
#include <termcolor/termcolor.hpp>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <unistd.h>
#endif

namespace mpi {

bool Output::isTerminal() {
    return isatty(STDOUT_FILENO) != 0;
}

void Output::info(const std::string& message) {
    if (isTerminal()) {
        std::cout << termcolor::blue << "==> " << termcolor::reset << message << "\n";
    } else {
        std::cout << "==> " << message << "\n";
    }
}

void Output::success(const std::string& message) {
    if (isTerminal()) {
        std::cout << termcolor::green << "==> " << termcolor::reset << message << "\n";
    } else {
        std::cout << "==> " << message << "\n";
    }
}

void Output::warn(const std::string& message) {
    if (isTerminal()) {
        std::cout << termcolor::yellow << "==> WARNING: " << termcolor::reset << message << "\n";
    } else {
        std::cout << "==> WARNING: " << message << "\n";
    }
}

void Output::error(const std::string& message) {
    if (isTerminal()) {
        std::cerr << termcolor::red << "==> ERROR: " << termcolor::reset << message << "\n";
    } else {
        std::cerr << "==> ERROR: " << message << "\n";
    }
}

void Output::print(const std::string& message) {
    std::cout << message << "\n";
}

void Output::printError(const std::string& message) {
    std::cerr << message << "\n";
}

void Output::printHeader() {
    std::cout << R"(
  __  __                _     _            ____ ___
 |  \/  | __ _ ___  ___| |__ (_)_ __   ___|  _ \_ _|
 | |\/| |/ _` / __|/ __| '_ \| | '_ \ / _ \ |_) | |
 | |  | | (_| \__ \ (__| | | | | | | |  __/  __/| |
 |_|  |_|\__,_|___/\___|_| |_|_|_| |_|\___|_|  |___|

)" << "\n";
}

} // namespace mpi
