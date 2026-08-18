#include "TestCommand.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mpi {
namespace fs = std::filesystem;

void TestCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("test", "Run unit tests via ctest");

    // Flags
    static bool withCoverage = false;
    cmd->add_flag("--coverage", withCoverage, "Generate code coverage report");

    cmd->callback([&]() {
        std::exit(execute(withCoverage));
    });
}

int TestCommand::execute(bool withCoverage) {
    auto buildDir = PathResolver::buildDir();
    auto projectRoot = PathResolver::getProjectRoot();

    // Check if build directory exists
    if (!fs::exists(buildDir)) {
        log_error("Build directory not found: " + buildDir.string());
        Output::print("");
        Output::print("  Run 'mpi build' first to build the project.");
        Output::print("");
        return 1;
    }

    // Check if CMakeCache.txt exists (indicates successful cmake configure)
    auto cmakeCachePath = buildDir / "CMakeCache.txt";
    if (!fs::exists(cmakeCachePath)) {
        log_error("CMake cache not found in: " + buildDir.string());
        Output::print("");
        Output::print("  Run 'mpi build' first to configure the project.");
        Output::print("");
        return 1;
    }

    // Handle coverage flag
    if (withCoverage) {
        log_info("Running tests with coverage");

        // Check if coverage is already enabled
        bool coverageEnabled = false;
        std::ifstream cacheFile(cmakeCachePath);
        if (cacheFile.is_open()) {
            std::string line;
            while (std::getline(cacheFile, line)) {
                if (line.find("ENABLE_COVERAGE:BOOL=ON") != std::string::npos) {
                    coverageEnabled = true;
                    break;
                }
            }
        }

        // Rebuild with coverage if not enabled
        if (!coverageEnabled) {
            log_info("Rebuilding with coverage enabled...");
            Output::print("");
            std::cout.flush();

            int configResult = ProcessExecutor::runStreaming(
                "cmake -DENABLE_COVERAGE=ON .",
                buildDir
            );
            if (configResult != 0) {
                log_error("CMake configuration failed");
                return configResult;
            }

            // Get nproc for parallel build
            std::string nproc = "4";
            try {
                auto result = ProcessExecutor::run("nproc");
                if (result.success()) {
                    nproc = result.stdout;
                    if (!nproc.empty() && nproc.back() == '\n') {
                        nproc.pop_back();
                    }
                }
            } catch (...) {
                // Use fallback
            }

            Output::print("");
            log_info("Building with coverage...");
            std::cout.flush();

            int buildResult = ProcessExecutor::runStreaming(
                "cmake --build . -j" + nproc,
                buildDir
            );
            if (buildResult != 0) {
                log_error("Build failed");
                return buildResult;
            }
            Output::print("");
        }
    } else {
        log_info("Running tests");
    }

    // Get nproc for parallel test execution
    std::string nproc = "4";
    try {
        auto result = ProcessExecutor::run("nproc");
        if (result.success()) {
            nproc = result.stdout;
            if (!nproc.empty() && nproc.back() == '\n') {
                nproc.pop_back();
            }
        }
    } catch (...) {
        // Use fallback
    }

    // Run ctest
    std::cout.flush();
    int ctestResult = ProcessExecutor::runStreaming(
        "ctest --output-on-failure -j" + nproc,
        buildDir
    );

    // Generate coverage report if requested and tests passed
    if (withCoverage && ctestResult == 0) {
        if (ProcessExecutor::commandExists("gcovr")) {
            Output::print("");
            log_info("Generating coverage report...");
            std::cout.flush();

            // Build gcovr command matching bash script
            std::ostringstream gcovCmd;
            gcovCmd << "gcovr -r " << projectRoot.string();
            gcovCmd << " --filter " << projectRoot.string() << "/src";
            gcovCmd << " --exclude " << projectRoot.string() << "/external";
            gcovCmd << " --exclude " << projectRoot.string() << "/tests";
            gcovCmd << " --gcov-ignore-errors=no_working_dir_found";
            gcovCmd << " --gcov-ignore-errors=source_not_found";
            gcovCmd << " --print-summary";

            // Run from build directory where .gcda files are located
            ProcessExecutor::runStreaming(gcovCmd.str(), buildDir);
        } else {
            Output::print("");
            log_warn("gcovr not found, skipping coverage report");
            Output::print("  Install with: pip install gcovr");
        }
    }

    // Final status
    Output::print("");
    if (ctestResult == 0) {
        log_success("Tests complete!");
    } else {
        log_error("Tests failed");
    }
    Output::print("");

    return ctestResult;
}

} // namespace mpi
