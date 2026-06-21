#!/bin/bash
set -e

MODS_DIR="/home/megax/.local/share/Steam/steamapps/common/Mewgenics/Mods"
CLEANUP=0

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-clang"

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p "${BUILD_DIR}"
fi

# Configure using Clang MSVC Toolchain and Ninja generator
if ! cmake -G Ninja \
           -DCMAKE_TOOLCHAIN_FILE="${PROJECT_DIR}/clang-msvc-toolchain.cmake" \
           -DCMAKE_BUILD_TYPE=RelWithDebInfo \
           -B "${BUILD_DIR}" \
           -S "${PROJECT_DIR}" > /dev/null; then
    echo "CMake configuration failed!"
    exit 1
fi

echo "Compiling resources..."
echo "Compiling Mewtiplayer..."

if ! cmake --build "${BUILD_DIR}" --config RelWithDebInfo; then
    echo "Compilation failed!"
    exit 1
fi

echo "Compilation successful."

if [ "${CLEANUP}" = "1" ]; then
    echo "Cleaning up build files..."
    rm -rf "${BUILD_DIR}"
fi

echo "Done!"
