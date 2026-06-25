#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${PROJECT_DIR}/dist"
BUILD_DIR="${PROJECT_DIR}/build"

echo "========================================="
echo "Building sip-answer-engine v0.3 package..."
echo "========================================="

mkdir -p "${BUILD_DIR}"

if command -v cmake >/dev/null 2>&1; then
    echo "Using CMake build flow..."
    cd "${BUILD_DIR}"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc)"
elif command -v g++ >/dev/null 2>&1; then
    echo "CMake not found. Falling back to direct g++ build..."
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
        -I"${PROJECT_DIR}/src" \
        "${PROJECT_DIR}"/src/*.cpp \
        -o "${BUILD_DIR}/sip-answer-engine"
else
    echo "Error: neither cmake nor g++ is available."
    echo "Install build tools first, for example: sudo apt install -y build-essential cmake"
    exit 1
fi

echo "========================================="
echo "Build succeeded. Packaging dist directory..."
echo "========================================="

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/bin"

cp "${BUILD_DIR}/sip-answer-engine" "${DIST_DIR}/bin/"
cp "${PROJECT_DIR}/config.yaml" "${DIST_DIR}/"
cp "${PROJECT_DIR}/start.sh" "${DIST_DIR}/"
cp "${PROJECT_DIR}/stop.sh" "${DIST_DIR}/"
cp "${PROJECT_DIR}/test.sh" "${DIST_DIR}/"

cp -r "${PROJECT_DIR}/tests" "${DIST_DIR}/"
if [ -d "${PROJECT_DIR}/deploy" ]; then cp -r "${PROJECT_DIR}/deploy" "${DIST_DIR}/"; fi
if [ -d "${PROJECT_DIR}/docs" ]; then cp -r "${PROJECT_DIR}/docs" "${DIST_DIR}/"; fi
if [ -d "${PROJECT_DIR}/scripts" ]; then cp -r "${PROJECT_DIR}/scripts" "${DIST_DIR}/"; fi

chmod +x "${DIST_DIR}/bin/sip-answer-engine"
chmod +x "${DIST_DIR}/start.sh"
chmod +x "${DIST_DIR}/stop.sh"
chmod +x "${DIST_DIR}/test.sh"
if [ -f "${DIST_DIR}/scripts/stress_1000_10h.sh" ]; then chmod +x "${DIST_DIR}/scripts/stress_1000_10h.sh"; fi

echo "-> Package completed: ${DIST_DIR}"
echo "You can copy the dist directory to the target Kylin/Linux host."
echo "========================================="
