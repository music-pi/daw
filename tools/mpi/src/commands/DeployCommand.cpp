#include "DeployCommand.h"
#include "../core/Config.h"
#include "../core/Output.h"
#include "../core/PathResolver.h"
#include "../core/ProcessExecutor.h"
#include "../core/ElfChecker.h"
#include <sstream>
#include <string>

namespace mpi {

void DeployCommand::setup(CLI::App& app) {
    auto* cmd = app.add_subcommand("deploy", "Deploy ARM64 binary to Raspberry Pi");

    // Load config for defaults (uses hardcoded defaults if no file)
    auto config = Config::load();
    const auto& dc = config.deploy();

    static std::string host;
    static std::string user;
    static std::string remotePath;
    static std::string service;
    static bool skipRestart = false;

    host = dc.host;
    user = dc.user;
    remotePath = dc.remotePath;
    service = dc.service;

    cmd->add_option("-H,--host", host,
        "Pi hostname or IP address")
        ->default_val(dc.host);

    cmd->add_option("-u,--user", user,
        "SSH username")
        ->default_val(dc.user);

    cmd->add_option("-p,--path", remotePath,
        "Remote binary path")
        ->default_val(dc.remotePath);

    cmd->add_option("-s,--service", service,
        "Systemd service name")
        ->default_val(dc.service);

    cmd->add_flag("--no-restart", skipRestart,
        "Skip service restart after deploy");

    cmd->callback([]() {
        std::exit(execute(host, user, remotePath, service, skipRestart));
    });
}

bool DeployCommand::testSshConnection(const std::string& host, const std::string& user) {
    std::ostringstream cmd;
    cmd << "ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new "
        << user << "@" << host << " exit 2>/dev/null";

    int exitCode = ProcessExecutor::runStreaming(cmd.str());
    return exitCode == 0;
}

int DeployCommand::execute(const std::string& host, const std::string& user,
                           const std::string& remotePath, const std::string& service,
                           bool skipRestart) {
    log_info("Deploying to " + user + "@" + host + "...");
    Output::print("");

    // Step 1: Find Pi binary
    auto piBinaryOpt = PathResolver::findPiBinary();
    if (!piBinaryOpt) {
        log_error("ARM64 binary not found");
        Output::printError("");
        Output::printError("The Pi build directory doesn't contain a maschinepi binary.");
        Output::printError("Run 'mpi build pi' first to cross-compile for Raspberry Pi.");
        return 1;
    }
    auto piBinary = *piBinaryOpt;
    log_success("Found binary: " + piBinary.filename().string());

    // Step 2: Verify ARM64 architecture
    if (!ElfChecker::isArm64Binary(piBinary)) {
        log_error("Binary is not ARM64 architecture");
        Output::printError("");
        Output::printError("The binary at " + piBinary.string() + " is not an ARM64 ELF.");
        Output::printError("This may be an x86_64 build. Run 'mpi build pi' to cross-compile.");
        return 1;
    }
    log_success("Verified ARM64 architecture");

    // Step 3: Test SSH connection
    log_info("Testing SSH connection to " + host + "...");
    if (!testSshConnection(host, user)) {
        log_error("Cannot connect to " + user + "@" + host);
        Output::printError("");
        Output::printError("SSH connection failed. Please check:");
        Output::printError("  - Is the Pi reachable? Try: ping " + host);
        Output::printError("  - Is SSH key authentication set up? Run: ssh-copy-id " + user + "@" + host);
        Output::printError("  - Wrong host? Use: mpi deploy --host <hostname>");
        return 1;
    }
    log_success("SSH connection OK");

    // Step 4: Deploy binary using rsync (atomic transfer)
    log_info("Transferring binary...");
    std::ostringstream rsyncCmd;
    rsyncCmd << "rsync -az --progress -e 'ssh -o BatchMode=yes' "
             << "--rsync-path='sudo rsync' "
             << piBinary.string() << " "
             << user << "@" << host << ":" << remotePath;

    int rsyncResult = ProcessExecutor::runStreaming(rsyncCmd.str());
    if (rsyncResult != 0) {
        log_error("Failed to transfer binary (exit code " + std::to_string(rsyncResult) + ")");
        return rsyncResult;
    }
    log_success("Binary transferred to " + remotePath);

    // Step 5: Restart service (unless skipped)
    if (!skipRestart) {
        log_info("Restarting " + service + " service...");
        std::ostringstream sshCmd;
        sshCmd << "ssh -o BatchMode=yes " << user << "@" << host
               << " 'sudo systemctl restart " << service << "'";

        int restartResult = ProcessExecutor::runStreaming(sshCmd.str());
        if (restartResult != 0) {
            log_warn("Service restart failed (exit code " + std::to_string(restartResult) + ")");
            Output::print("");
            Output::print("Binary was deployed successfully, but service restart failed.");
            Output::print("Manually restart with: ssh " + user + "@" + host + " 'sudo systemctl restart " + service + "'");
            return restartResult;
        }
        log_success("Service restarted");
    } else {
        log_info("Skipping service restart (--no-restart)");
    }

    // Step 6: Success summary
    Output::print("");
    log_success("Deploy complete!");
    Output::print("");
    Output::print("  Host:    " + user + "@" + host);
    Output::print("  Binary:  " + remotePath);
    Output::print("  Service: " + std::string(skipRestart ? "not restarted" : service));
    Output::print("");

    return 0;
}

} // namespace mpi
