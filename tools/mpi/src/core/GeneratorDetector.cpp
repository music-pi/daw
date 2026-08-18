#include "GeneratorDetector.h"
#include "ProcessExecutor.h"

namespace mpi {

bool GeneratorDetector::hasNinja() {
    return ProcessExecutor::commandExists("ninja");
}

std::string GeneratorDetector::detect() {
    if (hasNinja()) {
        return "Ninja";
    }
    return "Unix Makefiles";
}

} // namespace mpi
