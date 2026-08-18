#pragma once
#include <string>

namespace mpi {

/**
 * Detects available CMake generators (Ninja vs Make).
 */
class GeneratorDetector {
public:
    /**
     * Detect best available generator.
     * Prefers Ninja if available, falls back to Unix Makefiles.
     * @return Generator name for cmake -G flag
     */
    static std::string detect();

    /**
     * Check if Ninja is available.
     * @return true if ninja command exists in PATH
     */
    static bool hasNinja();
};

} // namespace mpi
