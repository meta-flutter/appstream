#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
#
# Single source of truth for configuring the CMake build. scripts/test.sh and
# .github/workflows/ci.yml both call this instead of spelling out -D flags.
#
# The flag names are the reason this exists. The test suite gate was renamed
# to APPSTREAM_BUILD_TESTS in 0.2.2, but copies of the old -DBUILD_TESTING
# lived on in the test script and in two CI jobs, where CMake ignored them
# silently ("Manually-specified variables were not used by the project").
# The C++ suite was therefore never configured by scripts/test.sh, and ctest
# ran whatever stale binary was left in the build directory. One definition
# is harder to leave behind than four.
#
# Usage:
#   scripts/configure.sh [options]
#
#   --build-dir DIR       CMake binary directory     (default: build)
#   --build-type TYPE     CMAKE_BUILD_TYPE           (default: Release)
#   --tests ON|OFF        build the C++ test suite   (default: OFF)
#   --sanitizer NAME      none|asan|msan|ubsan       (default: none)
#   --coverage ON|OFF     gcov/lcov instrumentation  (default: OFF)
#   --benchmarks ON|OFF   build benchmark targets    (default: OFF)
#   --compile-commands    emit compile_commands.json (for clang-tidy)
#
# Anything after `--` is passed through to cmake verbatim.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_DIR=build
BUILD_TYPE=Release
TESTS=OFF
SANITIZER=none
COVERAGE=OFF
BENCHMARKS=OFF
COMPILE_COMMANDS=OFF
EXTRA=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE="$2"
      shift 2
      ;;
    --tests)
      TESTS="$2"
      shift 2
      ;;
    --sanitizer)
      SANITIZER="$2"
      shift 2
      ;;
    --coverage)
      COVERAGE="$2"
      shift 2
      ;;
    --benchmarks)
      BENCHMARKS="$2"
      shift 2
      ;;
    --compile-commands)
      COMPILE_COMMANDS=ON
      shift
      ;;
    --)
      shift
      EXTRA=("$@")
      break
      ;;
    -h | --help)
      echo "usage: scripts/configure.sh [--build-dir DIR] [--build-type TYPE]"
      echo "         [--tests ON|OFF] [--sanitizer none|asan|msan|ubsan]"
      echo "         [--coverage ON|OFF] [--benchmarks ON|OFF]"
      echo "         [--compile-commands] [-- <extra cmake args>]"
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      exit 2
      ;;
  esac
done

# Reject unknown values rather than passing them to CMake, which would take
# an unrecognized sanitizer as a literal and configure a build that silently
# does not do what was asked.
case "$TESTS" in ON | OFF) ;; *)
  echo "error: --tests must be ON or OFF, got '$TESTS'" >&2
  exit 2
  ;;
esac
case "$COVERAGE" in ON | OFF) ;; *)
  echo "error: --coverage must be ON or OFF, got '$COVERAGE'" >&2
  exit 2
  ;;
esac
case "$BENCHMARKS" in ON | OFF) ;; *)
  echo "error: --benchmarks must be ON or OFF, got '$BENCHMARKS'" >&2
  exit 2
  ;;
esac
case "$SANITIZER" in none | asan | msan | ubsan) ;; *)
  echo "error: --sanitizer must be none|asan|msan|ubsan, got '$SANITIZER'" >&2
  exit 2
  ;;
esac

GEN_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GEN_ARGS+=(-G Ninja)
fi

set -x
cmake -S . -B "$BUILD_DIR" \
  "${GEN_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS="$COMPILE_COMMANDS" \
  -DAPPSTREAM_BUILD_TESTS="$TESTS" \
  -DENABLE_SANITIZER="$SANITIZER" \
  -DENABLE_COVERAGE="$COVERAGE" \
  -DENABLE_BENCHMARKS="$BENCHMARKS" \
  ${EXTRA+"${EXTRA[@]}"}
