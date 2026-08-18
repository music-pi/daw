#!/usr/bin/env bash
#
# Bootstrap script: builds the mpi C++ CLI and places binary at project root.
#
# Usage:
#   ./scripts/build-cli.sh
#
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"

# Colors
if [[ -t 1 ]]; then
  GREEN='\033[0;32m'; BLUE='\033[0;34m'; RED='\033[0;31m'; NC='\033[0m'
else
  GREEN='' BLUE='' RED='' NC=''
fi
log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}==> ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

# Check prerequisites
for tool in cmake g++; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    log_error "${tool} not found. Install build dependencies first:"
    echo "  sudo apt install build-essential cmake"
    exit 1
  fi
done

log_info "Building mpi CLI..."

cd "${ROOT_DIR}/tools/mpi"
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1

log_info "Compiling..."
cmake --build . -j"$(nproc)" 2>&1

# Copy to project root
cp "${ROOT_DIR}/tools/mpi/bin/mpi" "${ROOT_DIR}/mpi"
chmod +x "${ROOT_DIR}/mpi"

echo ""
log_success "Built ./mpi successfully"
echo ""
echo "  Run './mpi --help' to see available commands."
echo ""
