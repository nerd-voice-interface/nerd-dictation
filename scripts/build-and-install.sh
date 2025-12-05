#!/bin/bash
# Скрипт для сборки и локальной установки проекта

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/install"

cd "$PROJECT_ROOT"

echo "=== Сборка и установка nerd-dictation-ex ==="
echo "Build directory: ${BUILD_DIR}"
echo "Install directory: ${INSTALL_DIR}"
echo ""

# Создаём директории
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_DIR}"

# Конфигурация CMake
cd "${BUILD_DIR}"
cmake .. \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_VOSK_BACKEND=ON \
    -DENABLE_GIGAAM_BACKEND=OFF

# Сборка
echo ""
echo "=== Сборка проекта ==="
make -j$(nproc)

# Установка
echo ""
echo "=== Установка в ${INSTALL_DIR} ==="
make install

echo ""
echo "=== Установка завершена ==="
echo ""
echo "Установленные файлы:"
find "${INSTALL_DIR}" -type f 2>/dev/null | head -20
echo ""
echo "Для использования добавьте в PATH:"
echo "  export PATH=\"${INSTALL_DIR}/bin:\$PATH\""
echo ""
echo "Или используйте напрямую:"
echo "  ${INSTALL_DIR}/bin/nerd-dictation-ex --help"
