#!/usr/bin/env bash
# FluxCache build script: auto-resolves vcpkg + minio-cpp and builds.
# Usage: ./build.sh [build_dir] [cmake_args...]
#   build_dir: default "build"
#   cmake_args: e.g. -DFLUXCACHE_ENABLE_FUSE=ON

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
# First arg is build dir if it doesn't look like a cmake option
if [[ -n "$1" && "$1" != -* ]]; then
  BUILD_DIR="$1"
  shift
fi
CMAKE_EXTRA=("$@")
BUILD_PATH="$PROJECT_ROOT/$BUILD_DIR"

# vcpkg: prefer VCPKG_ROOT, else use project's vcpkg/
VCPKG_ROOT="${VCPKG_ROOT:-$PROJECT_ROOT/vcpkg}"
VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

if [[ ! -f "$VCPKG_TOOLCHAIN" ]]; then
  echo "vcpkg not found at $VCPKG_ROOT"
  echo "  Set VCPKG_ROOT to your vcpkg path, or ensure vcpkg/ exists in project root."
  exit 1
fi

# Bootstrap vcpkg if vcpkg binary missing
VCPKG_BIN="$VCPKG_ROOT/vcpkg"
if [[ ! -x "$VCPKG_BIN" ]]; then
  echo "Bootstrapping vcpkg..."
  (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh)
fi

echo "Using vcpkg: $VCPKG_ROOT"
echo "Build dir:   $BUILD_PATH"

mkdir -p "$BUILD_PATH"
cd "$BUILD_PATH"

cmake "$PROJECT_ROOT" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
  "${CMAKE_EXTRA[@]}"

# Symlink compile_commands.json to project root for clangd / C++ IntelliSense
ln -sf "$(pwd)/compile_commands.json" "$PROJECT_ROOT/compile_commands.json" 2>/dev/null || true

cmake --build .

echo ""
echo "Build complete. Run tests: cd $BUILD_PATH && ctest --output-on-failure"
