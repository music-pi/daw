#include "ElfChecker.h"
#include <elf.h>
#include <fstream>

namespace mpi {

bool ElfChecker::isArm64Binary(const std::filesystem::path& binaryPath) {
    std::ifstream file(binaryPath, std::ios::binary);
    if (!file) return false;

    // Read ELF header (64 bytes for ELF64)
    Elf64_Ehdr header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) return false;

    // Check ELF magic number: 0x7f 'E' 'L' 'F'
    if (header.e_ident[EI_MAG0] != ELFMAG0 ||
        header.e_ident[EI_MAG1] != ELFMAG1 ||
        header.e_ident[EI_MAG2] != ELFMAG2 ||
        header.e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    // Check 64-bit class
    if (header.e_ident[EI_CLASS] != ELFCLASS64) {
        return false;
    }

    // Check AArch64 machine type (value 183)
    return header.e_machine == EM_AARCH64;
}

} // namespace mpi
