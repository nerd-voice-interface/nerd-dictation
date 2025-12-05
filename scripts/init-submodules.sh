#!/bin/bash
# Скрипт для инициализации git submodules

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
THIRDPARTY_DIR="$PROJECT_ROOT/ThirdParty"

cd "$PROJECT_ROOT"

echo "Инициализация git submodules в $THIRDPARTY_DIR..."

# Создаём директорию если её нет
mkdir -p "$THIRDPARTY_DIR"

# TOML11 - header-only TOML парсер
if [ ! -d "$THIRDPARTY_DIR/toml11" ]; then
    echo "Добавляем toml11..."
    git submodule add https://github.com/ToruNiina/toml11.git "$THIRDPARTY_DIR/toml11"
else
    echo "toml11 уже существует, пропускаем..."
fi

# spdlog - быстрая библиотека логирования
if [ ! -d "$THIRDPARTY_DIR/spdlog" ]; then
    echo "Добавляем spdlog..."
    git submodule add https://github.com/gabime/spdlog.git "$THIRDPARTY_DIR/spdlog"
else
    echo "spdlog уже существует, пропускаем..."
fi

# CLI11 - header-only CLI парсер
if [ ! -d "$THIRDPARTY_DIR/CLI11" ]; then
    echo "Добавляем CLI11..."
    git submodule add https://github.com/CLIUtils/CLI11.git "$THIRDPARTY_DIR/CLI11"
else
    echo "CLI11 уже существует, пропускаем..."
fi

# Eigen - линейная алгебра (опционально)
if [ ! -d "$THIRDPARTY_DIR/eigen" ]; then
    echo "Добавляем Eigen (опционально)..."
    git submodule add https://gitlab.com/libeigen/eigen.git "$THIRDPARTY_DIR/eigen"
else
    echo "Eigen уже существует, пропускаем..."
fi

# Инициализация всех submodules
echo ""
echo "Инициализация всех submodules..."
git submodule update --init --recursive

echo ""
echo "Готово! Все submodules инициализированы в $THIRDPARTY_DIR"
echo ""
echo "Для обновления submodules используйте:"
echo "  git submodule update --remote --recursive"
