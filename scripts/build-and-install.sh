#!/bin/bash
# Script for building and local installation of the project

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/install"

cd "$PROJECT_ROOT"

echo "=== Building and installing nerd-dictation ==="
echo "Build directory: ${BUILD_DIR}"
echo "Install directory: ${INSTALL_DIR}"
echo ""

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_DIR}"

# CMake configuration
cd "${BUILD_DIR}"
cmake .. \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_GIGAAM_BACKEND=OFF

# Build
echo ""
echo "=== Building project ==="
make -j$(nproc)

# Install
echo ""
echo "=== Installing to ${INSTALL_DIR} ==="
make install

echo ""
echo "=== Installation completed ==="
echo ""
echo "Installed files:"
find "${INSTALL_DIR}" -type f 2>/dev/null | head -20
echo ""
echo "To use, add to PATH:"
echo "  export PATH=\"${INSTALL_DIR}/bin:\$PATH\""
echo ""
echo "Or use directly:"
echo "  ${INSTALL_DIR}/bin/nerd-dictation --help"
