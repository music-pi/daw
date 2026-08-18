#include "BuildCommand.h"
#include "../core/Config.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include "../core/BuildConfig.h"
#include "../core/GeneratorDetector.h"
#include "../core/CcacheSetup.h"
#include "../core/Arm64Toolchain.h"
#include <filesystem>
#include <sstream>

namespace mpi {
namespace fs = std::filesystem;

void BuildCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("build", "Build the project");

    // Build target (positional, optional — default from config)
    auto config = Config::load();
    static std::string target = config.build().defaultTarget;
    cmd->add_option("target", target, "Build target")
       ->default_str(config.build().defaultTarget)
       ->check(CLI::IsMember({"headless", "desktop", "pi", "tools", "debug", "release"}));

    // Flags
    static bool debug = false;
    static bool release = false;
    cmd->add_flag("--debug", debug, "Build with debug symbols");
    cmd->add_flag("--release", release, "Build optimized release");

    // Pi-specific flags (stubs for now)
    static bool installDeps = false;
    static bool checkToolchain = false;
    cmd->add_flag("--install-deps", installDeps, "Install ARM64 cross-compilation dependencies");
    cmd->add_flag("--check", checkToolchain, "Check if ARM64 toolchain is installed");

    static bool emulatorMode = false;
    cmd->add_flag("--emu", emulatorMode, "(Deprecated) Emulator is now built-in at runtime");

    cmd->callback([&]() {
        execute(target, debug, release, checkToolchain, installDeps, emulatorMode);
    });
}

int BuildCommand::execute(const std::string& targetStr, bool debugFlag, bool releaseFlag,
                          bool checkFlag, bool installDepsFlag, bool emuFlag) {
    // Parse target and type from arguments
    BuildTarget target = BuildTarget::Headless;
    BuildType type = BuildType::Release;

    // Determine build target
    if (targetStr == "headless") {
        target = BuildTarget::Headless;
    } else if (targetStr == "desktop") {
        target = BuildTarget::Desktop;
    } else if (targetStr == "tools") {
        target = BuildTarget::Tools;
    } else if (targetStr == "pi" || targetStr == "raspberry") {
        target = BuildTarget::Pi;
    } else if (targetStr == "debug") {
        // "debug" can be treated as a type modifier
        type = BuildType::Debug;
        target = BuildTarget::Headless;  // Default target
    } else if (targetStr == "release") {
        // "release" explicitly sets release type
        type = BuildType::Release;
        target = BuildTarget::Headless;  // Default target
    }

    // Override type if flags are set
    if (debugFlag) {
        type = BuildType::Debug;
    } else if (releaseFlag) {
        type = BuildType::Release;
    }

    // Handle Pi-specific flags
    if (target == BuildTarget::Pi) {
        // --check flag: verify toolchain and exit
        if (checkFlag) {
            bool ready = Arm64Toolchain::check(/* verbose= */ true);
            return ready ? 0 : 1;
        }

        // --install-deps flag: install dependencies and exit
        if (installDepsFlag) {
            bool success = Arm64Toolchain::installDeps();
            return success ? 0 : 1;
        }

        // Normal build: verify toolchain is available
        if (!Arm64Toolchain::check(/* verbose= */ false)) {
            log_error("ARM64 cross-compilation toolchain not found");
            Output::print("");

            auto missingCompilers = Arm64Toolchain::getMissingCompilers();
            if (!missingCompilers.empty()) {
                Output::print("  Missing compilers:");
                for (const auto& tool : missingCompilers) {
                    Output::print("    - " + tool);
                }
                Output::print("");
                Output::print("  Install with:");
                Output::print("    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu");
                Output::print("");
            }

            auto missingLibs = Arm64Toolchain::getMissingLibraries();
            if (!missingLibs.empty()) {
                Output::print("  Missing libraries:");
                for (const auto& lib : missingLibs) {
                    Output::print("    - " + lib);
                }
                Output::print("");
            }

            Output::print("  Or run: mpi build pi --install-deps");
            Output::print("");
            return 1;
        }
    }

    // Create build configuration
    BuildConfig config(target, type);
    auto buildDir = config.getBuildDir();
    auto projectRoot = PathResolver::getProjectRoot();

    // Create build directory if it doesn't exist
    try {
        fs::create_directories(buildDir);
    } catch (const std::exception& e) {
        log_error("Failed to create build directory: " + std::string(e.what()));
        return 1;
    }

    // Detect generator
    std::string generator = GeneratorDetector::detect();

    // Setup ccache if available
    std::string cacheSubdir = config.getTargetName();
    CcacheSetup ccache(cacheSubdir);

    if (CcacheSetup::isAvailable()) {
        ccache.setupEnvironment();
        log_info("ccache enabled (cache: " + ccache.getCacheDir().string() + ")");
        std::cout.flush();
    }

    // Log configuration
    log_info("Building for " + config.getTargetName());
    Output::print("  Build type: " + config.getBuildType());
    Output::print("  Generator:  " + generator);
    Output::print("");
    std::cout.flush();  // Ensure output appears before cmake streaming starts

    // Build cmake configure command
    std::ostringstream configCmd;
    configCmd << "cmake -G \"" << generator << "\"";
    configCmd << " -DCMAKE_BUILD_TYPE=" << config.getBuildType();
    configCmd << " -DDEV_DESKTOP=" << config.getDevDesktop();
    configCmd << " -DENABLE_TRACKTION=ON";
    configCmd << " -DBUILD_TOOLS=" << config.getBuildTools();
    configCmd << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

    // Add ccache compiler launcher flags
    auto ccacheArgs = ccache.getCMakeArgs();
    for (const auto& arg : ccacheArgs) {
        configCmd << " " << arg;
    }

    // Emulator support is now built-in at runtime (no cmake flag needed).
    // The --emu flag is kept for backward compat but has no effect.

    // Add target-specific cmake arguments (e.g., toolchain file for Pi)
    auto extraArgs = config.getExtraCMakeArgs();
    for (const auto& arg : extraArgs) {
        configCmd << " " << arg;
    }

    configCmd << " " << projectRoot.string();

    // Execute cmake configure
    log_info("Configuring...");
    int exitCode = ProcessExecutor::runStreaming(configCmd.str(), buildDir);
    if (exitCode != 0) {
        log_error("CMake configuration failed");
        return exitCode;
    }

    // Get number of CPU cores for parallel build
    std::string nproc = "$(nproc)";
    try {
        auto result = ProcessExecutor::run("nproc");
        if (result.success()) {
            nproc = result.stdout;
            // Remove trailing newline
            if (!nproc.empty() && nproc.back() == '\n') {
                nproc.pop_back();
            }
        }
    } catch (...) {
        nproc = "4";  // Fallback to 4 cores
    }

    // Build cmake build command
    std::ostringstream buildCmd;
    buildCmd << "cmake --build . -j" << nproc;

    // Execute cmake build
    Output::print("");
    log_info("Building (" + config.getBuildType() + ")");
    exitCode = ProcessExecutor::runStreaming(buildCmd.str(), buildDir);
    if (exitCode != 0) {
        log_error("Build failed");
        return exitCode;
    }

    // Success!
    Output::print("");
    log_success("Build complete!");
    Output::print("  Target:     " + config.getTargetName());
    if (target == BuildTarget::Pi) {
        Output::print("  Platform:   ARM64 (Raspberry Pi)");
    }
    Output::print("  Build type: " + config.getBuildType());
    Output::print("  Output:     " + buildDir.string() + "/bin/");
    Output::print("");

    return 0;
}

} // namespace mpi
