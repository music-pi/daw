#pragma once
#include <CLI/CLI.hpp>
#include <string>

namespace mpi {

class DeployCommand {
public:
    static void setup(CLI::App& app);

private:
    static int execute(const std::string& host, const std::string& user,
                       const std::string& remotePath, const std::string& service,
                       bool skipRestart);
    static bool testSshConnection(const std::string& host, const std::string& user);
};

} // namespace mpi
