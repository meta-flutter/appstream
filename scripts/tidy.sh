#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
#
# Runs clang-tidy at a pinned version over the project's C++ sources.
# .github/workflows/ci.yml calls this, so CI and a developer machine analyze
# the same code with the same checker.
#
# Why pinned, like scripts/format.sh: clang-tidy results vary sharply by
# version, and the differences here are not cosmetic.
#
#   18  Cannot parse this host's libstdc++ (GCC 16) — it bails with
#       "too many errors emitted, stopping now", so whatever it does report
#       comes from a half-parsed translation unit. It also emits a
#       bugprone-use-after-move false positive on `x = {}` immediately
#       following `std::move(x)`, which is the documented way to restore a
#       moved-from object.
#   20  Parses cleanly and reports the project's configured checks. It found
#       an unchecked gmtime_r return that 22 did not.
#   22  Parses cleanly but did not report that defect.
#
# The version below is therefore the one the project's .clang-tidy check set
# is known to work under. Left unpinned, the result depended on whether the
# caller had sourced an environment that put a different LLVM first on PATH,
# which is exactly how the false positive above went unexplained for a while.
#
# Usage:
#   scripts/tidy.sh           # analyze, non-zero exit on any diagnostic
#   scripts/tidy.sh --fix     # apply fixes clang-tidy considers safe

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

CLANG_TIDY_VERSION="${CLANG_TIDY_VERSION:-20.1.0}"
BUILD_DIR="${BUILD_DIR:-build}"

# Vendored sources are excluded for the same reason as in format.sh:
# dart_api_dl* is Dart SDK code and legitimately includes a .c file, which
# trips bugprone-suspicious-include.
SRC_DIR=src
EXCLUDES=(dart_api_dl dart_api_types)

FIX=0
for arg in "$@"; do
  case "$arg" in
    --fix) FIX=1 ;;
    -h | --help)
      echo "usage: scripts/tidy.sh [--fix]"
      exit 0
      ;;
    *)
      echo "unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

resolve_clang_tidy() {
  if command -v clang-tidy >/dev/null 2>&1 &&
    clang-tidy --version | grep -qF "$CLANG_TIDY_VERSION"; then
    command -v clang-tidy
    return
  fi

  local venv=".cache/clang-tidy/$CLANG_TIDY_VERSION"
  if [[ ! -x "$venv/bin/clang-tidy" ]]; then
    echo "Provisioning clang-tidy $CLANG_TIDY_VERSION into $venv" >&2
    python3 -m venv "$venv" >&2
    "$venv/bin/pip" install --quiet "clang-tidy==$CLANG_TIDY_VERSION" >&2
  fi
  echo "$venv/bin/clang-tidy"
}

# clang-tidy needs a compilation database. Generate one if absent rather than
# failing with a message about compile_commands.json that the caller then has
# to translate back into a cmake invocation.
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
  echo "No $BUILD_DIR/compile_commands.json; configuring" >&2
  ./scripts/configure.sh --build-dir "$BUILD_DIR" --tests OFF --compile-commands >&2
fi

CT="$(resolve_clang_tidy)"
echo "=== clang-tidy $("$CT" --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1) ($CT) ==="

find_args=()
for pattern in "${EXCLUDES[@]}"; do
  find_args+=(-not -name "$pattern*")
done

files=()
while IFS= read -r -d '' f; do
  files+=("$f")
done < <(find "$SRC_DIR" -type f \( -name '*.cpp' -o -name '*.hpp' \) \
  "${find_args[@]}" -print0)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "error: no C++ sources found in $SRC_DIR" >&2
  exit 1
fi

# clang-tidy is invoked directly rather than through run-clang-tidy: the pip
# package ships only the clang-tidy binary, and run-clang-tidy resolves
# `clang-tidy` from PATH by default, which is the drift this script exists to
# prevent.
#
# --warnings-as-errors is what makes this a gate. Plain clang-tidy exits 0
# even when it reports diagnostics, so without this the job would pass
# unconditionally — a check that cannot fail is worse than no check, because
# it reads as coverage.
TIDY_ARGS=(-p "$BUILD_DIR" --quiet --warnings-as-errors='*')

JOBS="$(nproc 2>/dev/null || echo 4)"
if [[ $FIX -eq 1 ]]; then
  # Fixes are applied serially: parallel workers editing headers included by
  # more than one translation unit can interleave edits.
  TIDY_ARGS+=(--fix)
  JOBS=1
fi

printf '%s\0' "${files[@]}" |
  xargs -0 -P "$JOBS" -n 1 "$CT" "${TIDY_ARGS[@]}"
