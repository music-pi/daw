#include "Arm64Toolchain.h"
#include "ProcessExecutor.h"
#include "Output.h"
#include <filesystem>

namespace mpi {
namespace fs = std::filesystem;

std::vector<std::string> Arm64Toolchain::getMissingCompilers() {
    std::vector<std::string> missing;

    // Check for aarch64 GCC cross-compiler
    if (!ProcessExecutor::commandExists("aarch64-linux-gnu-gcc")) {
        missing.push_back("aarch64-linux-gnu-gcc");
    }

    // Check for aarch64 G++ cross-compiler
    if (!ProcessExecutor::commandExists("aarch64-linux-gnu-g++")) {
        missing.push_back("aarch64-linux-gnu-g++");
    }

    return missing;
}

std::vector<std::string> Arm64Toolchain::getMissingLibraries() {
    std::vector<std::string> missing;

    // Check for ARM64 pkgconfig directory
    fs::path pkgconfigDir = "/usr/lib/aarch64-linux-gnu/pkgconfig";
    if (!fs::exists(pkgconfigDir)) {
        missing.push_back("ARM64 library directory (/usr/lib/aarch64-linux-gnu/pkgconfig)");
        // If the directory doesn't exist, the libraries definitely don't
        return missing;
    }

    // Check for required library pkgconfig files
    struct Library {
        std::string pcFile;
        std::string package;
    };

    std::vector<Library> requiredLibs = {
        {"libusb-1.0.pc", "libusb-1.0-0-dev:arm64"},
        {"freetype2.pc", "libfreetype-dev:arm64"},
        {"alsa.pc", "libasound2-dev:arm64"}
    };

    for (const auto& lib : requiredLibs) {
        fs::path libPath = pkgconfigDir / lib.pcFile;
        if (!fs::exists(libPath)) {
            missing.push_back(lib.package);
        }
    }

    return missing;
}

bool Arm64Toolchain::check(bool verbose) {
    auto missingCompilers = getMissingCompilers();
    auto missingLibraries = getMissingLibraries();

    bool isReady = missingCompilers.empty() && missingLibraries.empty();

    if (verbose) {
        if (isReady) {
            log_success("ARM64 cross-compilation environment ready");
            Output::print("  C:      /usr/bin/aarch64-linux-gnu-gcc");
            Output::print("  C++:    /usr/bin/aarch64-linux-gnu-g++");
            Output::print("  libusb: /usr/lib/aarch64-linux-gnu/pkgconfig/libusb-1.0.pc");
            Output::print("  alsa:   /usr/lib/aarch64-linux-gnu/pkgconfig/alsa.pc");
            Output::print("  freetype: /usr/lib/aarch64-linux-gnu/pkgconfig/freetype2.pc");
        } else {
            log_error("ARM64 cross-compilation toolchain not found");
            Output::print("");

            if (!missingCompilers.empty()) {
                Output::print("  Missing compilers:");
                for (const auto& tool : missingCompilers) {
                    Output::print("    - " + tool);
                }
                Output::print("");
            }

            if (!missingLibraries.empty()) {
                Output::print("  Missing libraries:");
                for (const auto& lib : missingLibraries) {
                    Output::print("    - " + lib);
                }
                Output::print("");
            }

            Output::print("  Install with:");
            Output::print("    mpi build pi --install-deps");
            Output::print("");
            Output::print("  Or manually:");
            Output::print("    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu");
            Output::print("    sudo apt install libusb-1.0-0-dev:arm64 libasound2-dev:arm64 libfreetype-dev:arm64");
            Output::print("");
        }
    }

    return isReady;
}

bool Arm64Toolchain::installDeps() {
    // Check for Debian/Ubuntu (apt-based system)
    if (!ProcessExecutor::commandExists("apt")) {
        log_error("This feature requires apt (Debian/Ubuntu)");
        return false;
    }

    // Print installation summary
    log_info("Installing ARM64 cross-compilation dependencies");
    Output::print("");
    Output::print("  This will install:");
    Output::print("    - ARM64 architecture support");
    Output::print("    - GCC/G++ ARM64 cross-compilers");
    Output::print("    - ARM64 development libraries (libusb, alsa, freetype)");
    Output::print("");
    log_warn("Requesting sudo access...");
    Output::print("");

    // Step 1: Add ARM64 architecture
    log_info("Adding ARM64 architecture support");
    int exitCode = ProcessExecutor::runStreaming("sudo dpkg --add-architecture arm64");
    if (exitCode != 0) {
        log_error("Failed to add ARM64 architecture");
        return false;
    }

    // Step 2: Get Ubuntu codename for ARM64 repos
    std::string codename = "noble";  // Default fallback
    try {
        auto result = ProcessExecutor::run("lsb_release -cs");
        if (result.success() && !result.stdout.empty()) {
            codename = result.stdout;
            // Remove trailing newline
            if (codename.back() == '\n') {
                codename.pop_back();
            }
        }
    } catch (...) {
        log_warn("Could not detect Ubuntu codename, using 'noble'");
    }

    // Step 3: Create ARM64 sources.list
    log_info("Configuring ARM64 package repositories");
    std::string sourcesContent =
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports " + codename + " main universe\n" +
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports " + codename + "-updates main universe\n" +
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports " + codename + "-security main universe\n";

    // Write sources.list using tee (requires sudo)
    std::string writeSourcesCmd =
        "echo '" + sourcesContent + "' | sudo tee /etc/apt/sources.list.d/arm64-cross.list > /dev/null";
    exitCode = ProcessExecutor::runStreaming(writeSourcesCmd);
    if (exitCode != 0) {
        log_error("Failed to configure ARM64 repositories");
        return false;
    }

    // Step 4: Update package lists
    log_info("Updating package lists");
    exitCode = ProcessExecutor::runStreaming("sudo apt update");
    if (exitCode != 0) {
        log_error("Failed to update package lists");
        return false;
    }

    // Step 5: Install cross-compilers
    Output::print("");
    log_info("Installing ARM64 cross-compilers");
    exitCode = ProcessExecutor::runStreaming(
        "sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    );
    if (exitCode != 0) {
        log_error("Failed to install cross-compilers");
        return false;
    }

    // Step 6: Install ARM64 libraries
    Output::print("");
    log_info("Installing ARM64 development libraries");
    exitCode = ProcessExecutor::runStreaming(
        "sudo apt install -y libusb-1.0-0-dev:arm64 libasound2-dev:arm64 "
        "libfreetype-dev:arm64 libfontconfig1-dev:arm64"
    );
    if (exitCode != 0) {
        log_error("Failed to install ARM64 libraries");
        return false;
    }

    // Success!
    Output::print("");
    log_success("ARM64 cross-compilation dependencies installed!");
    Output::print("");
    Output::print("  You can now build Pi binaries with:");
    Output::print("    mpi build pi");
    Output::print("");

    return true;
}

} // namespace mpi
