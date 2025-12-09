#!/bin/bash
# Script for initializing git submodules

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
THIRDPARTY_DIR="$PROJECT_ROOT/ThirdParty"

cd "$PROJECT_ROOT"

echo "Initializing git submodules in $THIRDPARTY_DIR..."

# Create directory if it doesn't exist
mkdir -p "$THIRDPARTY_DIR"

# TOML11 - header-only TOML parser
if [ ! -d "$THIRDPARTY_DIR/toml11" ]; then
    echo "Adding toml11..."
    git submodule add https://github.com/ToruNiina/toml11.git "$THIRDPARTY_DIR/toml11"
else
    echo "toml11 already exists, skipping..."
fi

# spdlog - fast logging library
if [ ! -d "$THIRDPARTY_DIR/spdlog" ]; then
    echo "Adding spdlog..."
    git submodule add https://github.com/gabime/spdlog.git "$THIRDPARTY_DIR/spdlog"
else
    echo "spdlog already exists, skipping..."
fi

# CLI11 - header-only CLI parser
if [ ! -d "$THIRDPARTY_DIR/CLI11" ]; then
    echo "Adding CLI11..."
    git submodule add https://github.com/CLIUtils/CLI11.git "$THIRDPARTY_DIR/CLI11"
else
    echo "CLI11 already exists, skipping..."
fi

# Eigen - linear algebra (required, for noise suppression)
if [ ! -d "$THIRDPARTY_DIR/eigen" ]; then
    echo "Adding Eigen (required for noise suppression)..."
    git submodule add https://gitlab.com/libeigen/eigen.git "$THIRDPARTY_DIR/eigen"
else
    echo "Eigen already exists, skipping..."
fi

# Initialize all submodules
echo ""
echo "Initializing all submodules..."
git submodule update --init --recursive

echo ""
echo "Done! All submodules initialized in $THIRDPARTY_DIR"
echo ""
echo "To update submodules use:"
echo "  git submodule update --remote --recursive"
