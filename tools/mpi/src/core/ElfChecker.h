#pragma once
#include <filesystem>

namespace mpi {

/**
 * Utility for verifying ELF binary architecture.
 * Uses direct ELF header parsing (no subprocess).
 */
class ElfChecker {
public:
    /**
     * Check if binary is ARM64 (AArch64) architecture.
     * Parses ELF header to verify magic number, 64-bit class, and EM_AARCH64 machine type.
     * @param binaryPath Path to ELF binary
     * @return true if binary is valid ARM64 ELF, false otherwise
     */
    static bool isArm64Binary(const std::filesystem::path& binaryPath);
};

} // namespace mpi
