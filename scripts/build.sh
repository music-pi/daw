#!/usr/bin/env bash
set -euo pipefail

# =================== Config & defaults ===================
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
cd "$ROOT_DIR"

BUILD_DIR="${BUILD_DIR:-build}"
TYPE="${TYPE:-Release}"          # Release|Debug|RelWithDebInfo|MinSizeRel
DEV_DESKTOP="${DEV_DESKTOP:-ON}" # ON = GUI-vindue; OFF = headless
ENABLE_TRACKTION="${ENABLE_TRACKTION:-ON}"
BUILD_TOOLS="${BUILD_TOOLS:-OFF}" # OFF = don't build tools; ON = build mpi-tools
RUN_AFTER="${RUN_AFTER:-${RUN:-0}}"  # RUN=1 env eller --run flag
DIST="${DIST:-0}"  # 0 = don't package; 1 = package to ./dist/
TRACE_INPUT="${TRACE_INPUT:-0}"  # 0 = don't trace; 1 = trace all input events

# =================== CLI flags ===========================
CLEAN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1; shift ;;
    --release) TYPE=Release; shift ;;
    --debug) TYPE=Debug; shift ;;
    --headless) DEV_DESKTOP=OFF; shift ;;
    --no-te) ENABLE_TRACKTION=OFF; shift ;;
    --build-tools) BUILD_TOOLS=ON; shift ;;
    --dist) DIST=1; shift ;;
    --trace-input) TRACE_INPUT=1; shift ;;
    --run) RUN_AFTER=1; shift ;;
    --type) TYPE="${2}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2}"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

# =================== Generator ===========================
GENERATOR="${CMAKE_GENERATOR:-}"
if command -v ninja >/dev/null 2>&1 ; then
  GENERATOR="${GENERATOR:-Ninja}"
else
  GENERATOR="${GENERATOR:-Unix Makefiles}"
fi

# =================== ccache (optional) ===================
USE_CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1 ; then
  export CCACHE_BASEDIR="${ROOT_DIR}"
  export CCACHE_DIR="${CCACHE_DIR:-${ROOT_DIR}/.ccache}"
  export CCACHE_TEMPDIR="${CCACHE_TEMPDIR:-${CCACHE_DIR}/tmp}"
  export CCACHE_COMPRESS="${CCACHE_COMPRESS:-1}"
  export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-10G}"
  mkdir -p "${CCACHE_DIR}"
  mkdir -p "${CCACHE_TEMPDIR}"
  ccache -o cache_dir="${CCACHE_DIR}" >/dev/null 2>&1 || true
  ccache -o temp_dir="${CCACHE_TEMPDIR}" >/dev/null 2>&1 || true
  ccache -M "${CCACHE_MAXSIZE}"       >/dev/null 2>&1 || true
  USE_CCACHE_ARGS+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

# =================== Clean ===============================
if [[ "$CLEAN" == "1" ]]; then
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# =================== Configure ===========================
echo
echo "==> Configuring (generator: ${GENERATOR})"
cmake \
  -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${TYPE}" \
  -DDEV_DESKTOP="${DEV_DESKTOP}" \
  -DENABLE_TRACKTION="${ENABLE_TRACKTION}" \
  -DBUILD_TOOLS="${BUILD_TOOLS}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  "${USE_CCACHE_ARGS[@]}" \
  ${CMAKE_ARGS:-} \
  ..

# =================== Build ===============================
echo
echo "==> Building (${TYPE})"
cmake --build . -j"${JOBS:-8}" -l"$(nproc)"

# =================== Summary =============================
echo
echo "---------------------------------------------------"
echo "  Build config"
echo "  DEV_DESKTOP       : ${DEV_DESKTOP}"
echo "  ENABLE_TRACKTION  : ${ENABLE_TRACKTION}"
echo "  BUILD_TOOLS       : ${BUILD_TOOLS}"
echo "  CMAKE_BUILD_TYPE  : ${TYPE}"
echo "  BUILD_DIR         : ${BUILD_DIR}"
echo "  GENERATOR         : ${GENERATOR}"
echo "---------------------------------------------------"
echo

# =================== Dist (optional) ======================
if [[ "${DIST}" == "1" ]]; then
  echo
  echo "==> Packaging artifacts to ./dist/"
  cd "${ROOT_DIR}"
  
  # Resolve absolute build directory path
  if [[ "${BUILD_DIR}" = /* ]]; then
    ABS_BUILD_DIR="${BUILD_DIR}"
  else
    ABS_BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
  fi
  
  rm -rf dist
  mkdir -p dist
  
  # Find and copy main binary
  BINARY=""
  if [[ -f "${ABS_BUILD_DIR}/maschinepi" ]] && [[ -x "${ABS_BUILD_DIR}/maschinepi" ]]; then
    BINARY="${ABS_BUILD_DIR}/maschinepi"
  elif [[ -f "${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/maschinepi" ]] && [[ -x "${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/maschinepi" ]]; then
    BINARY="${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/maschinepi"
  elif [[ -f "${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/Standalone/maschinepi" ]] && [[ -x "${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/Standalone/maschinepi" ]]; then
    BINARY="${ABS_BUILD_DIR}/maschinepi_artefacts/${TYPE}/Standalone/maschinepi"
  fi
  
  if [[ -n "${BINARY}" ]] && [[ -f "${BINARY}" ]]; then
    cp "${BINARY}" dist/maschinepi
    chmod +x dist/maschinepi
    echo "  -> Copied maschinepi binary"
  else
    echo "  WARNING: Could not find maschinepi binary to package"
  fi
  
  # Copy tools if built
  if [[ "${BUILD_TOOLS}" == "ON" ]] && [[ -d "${ABS_BUILD_DIR}/mpi-tools" ]]; then
    if [[ -n "$(ls -A "${ABS_BUILD_DIR}/mpi-tools" 2>/dev/null)" ]]; then
      cp -r "${ABS_BUILD_DIR}/mpi-tools" dist/
      echo "  -> Copied mpi-tools/"
    else
      echo "  -> mpi-tools/ directory is empty, skipping"
    fi
  fi
  
  echo "==> Distribution package created in ./dist/"
  echo
fi

# =================== Run (optional) ======================
if [[ "${RUN_AFTER}" == "1" ]]; then
  APP="./maschinepi"
  if [[ ! -x "${APP}" ]]; then
    # JUCE/Ninja plejer at smide binæren i build-roden for juce_add_gui_app
    # Fallbacks hvis layout ændres:
    if [[ -x "./maschinepi_artefacts/${TYPE}/maschinepi" ]]; then
      APP="./maschinepi_artefacts/${TYPE}/maschinepi"
    elif [[ -x "./maschinepi_artefacts/${TYPE}/Standalone/maschinepi" ]]; then
      APP="./maschinepi_artefacts/${TYPE}/Standalone/maschinepi"
    fi
  fi

  if [[ -x "${APP}" ]]; then
    echo "==> Running ${APP}"
    # Set trace input environment variable if flag is set
    if [[ "${TRACE_INPUT}" == "1" ]]; then
      export MK3_LOG_INPUTS=1
      export MASCHINEPI_TRACE_INPUT=1
      echo "==> Input tracing enabled (MK3_LOG_INPUTS=1, MASCHINEPI_TRACE_INPUT=1)"
    fi
    # Sikr at DISPLAY er sat på Linux/Wayland/X11
    if [[ -z "${DISPLAY:-}" ]] && [[ "$(uname)" == "Linux" ]] && [[ "${DEV_DESKTOP}" == "ON" ]]; then
      echo "WARNING: \$DISPLAY is empty. No GUI will appear (are you in a desktop session?)."
    fi
    exec pw-jack "${APP}"
  else
    echo "ERROR: Could not locate built app binary."
    echo ${APP}
    exit 2
  fi
fi

# =================== ccache stats ========================
if command -v ccache >/dev/null 2>&1 ; then
  echo
  ccache -s || true
fi
