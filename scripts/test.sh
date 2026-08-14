#!/usr/bin/env bash
# Unified test runner for appstream_dart.
#
# 1. Configures + builds the C++ test suite via CMake (Ninja if available).
# 2. Runs the C++ tests through ctest.
# 3. Runs the Dart test suite (which loads the FFI library built by
#    hook/build.dart on demand).
#
# Options (env vars):
#   BUILD_TYPE     CMake build type (default: Release)
#   BUILD_DIR      CMake build directory (default: build)
#   SANITIZER      none|asan|msan|ubsan (default: none)
#   COVERAGE       ON|OFF                (default: OFF)
#   BENCHMARKS     ON|OFF                (default: OFF)
#   SKIP_CXX       set to skip the C++ tests
#   SKIP_DART      set to skip the Dart tests
#
# Extra arguments are forwarded to `dart test`.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-build}"
SANITIZER="${SANITIZER:-none}"
COVERAGE="${COVERAGE:-OFF}"
BENCHMARKS="${BENCHMARKS:-OFF}"

if [[ -z "${SKIP_CXX:-}" ]]; then
  echo "=== Configuring CMake ($BUILD_TYPE) ==="
  GEN_ARGS=()
  if command -v ninja >/dev/null 2>&1; then
    GEN_ARGS+=(-G Ninja)
  fi
  cmake -S . -B "$BUILD_DIR" \
    "${GEN_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAPPSTREAM_BUILD_TESTS=ON \
    -DENABLE_SANITIZER="$SANITIZER" \
    -DENABLE_COVERAGE="$COVERAGE" \
    -DENABLE_BENCHMARKS="$BENCHMARKS"

  echo "=== Building C++ targets ==="
  cmake --build "$BUILD_DIR" --parallel

  echo "=== Running C++ tests (ctest) ==="
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ -z "${SKIP_DART:-}" ]]; then
  echo "=== Running Dart tests ==="
  dart pub get
  dart test "$@"
fi
