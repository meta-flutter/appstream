#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
#
# Single source of truth for formatting appstream_dart, used by both
# developers and CI (.github/workflows/ci.yml calls this script).
#
# Formatter output is version-sensitive: clang-format 22 and clang-format 18
# disagree about `struct stat sb {}`, and Dart 3.13 collapses some call
# arguments differently than 3.12. A tree formatted by the wrong version
# passes locally and fails CI, so the versions below are pinned and this
# script provisions the pinned clang-format rather than trusting $PATH.
#
# Usage:
#   scripts/format.sh            # apply formatting (C++ and Dart)
#   scripts/format.sh --check    # verify only, non-zero exit on violation
#   scripts/format.sh --cxx      # C++ only
#   scripts/format.sh --dart     # Dart only
#
# Options combine, e.g. `scripts/format.sh --check --cxx`.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# ── Pinned toolchain ───────────────────────────────────────────────────────
# Keep DART_SDK_VERSION in sync with the `sdk:` pin in ci.yml's format job.
CLANG_FORMAT_VERSION="${CLANG_FORMAT_VERSION:-18.1.8}"
DART_SDK_VERSION="${DART_SDK_VERSION:-3.13.0}"

# ── C++ sources to format ──────────────────────────────────────────────────
# Vendored and third-party headers are excluded: dart_api_dl* and
# dart_api_types* come from the Dart SDK, expected_polyfill* and spdlog* are
# upstream copies. Reformatting them would churn on every vendor update.
CXX_DIRS=(src include)
CXX_EXCLUDES=(dart_api_dl dart_api_types expected_polyfill spdlog)

# ── Dart sources to format ─────────────────────────────────────────────────
# example/flathub_catalog/ is deliberately absent: it is a Flutter
# sub-package formatted by the Flutter SDK's bundled dart format in the
# flutter-flathub-catalog CI job, which pins its own version.
DART_PATHS=(lib test hook bin example/example.dart)

CHECK=0
DO_CXX=0
DO_DART=0

for arg in "$@"; do
  case "$arg" in
    --check) CHECK=1 ;;
    --cxx) DO_CXX=1 ;;
    --dart) DO_DART=1 ;;
    -h | --help)
      echo "usage: scripts/format.sh [--check] [--cxx] [--dart]"
      exit 0
      ;;
    *)
      echo "unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

# Neither target named means both.
if [[ $DO_CXX -eq 0 && $DO_DART -eq 0 ]]; then
  DO_CXX=1
  DO_DART=1
fi

# Resolve a clang-format matching CLANG_FORMAT_VERSION, provisioning one into
# .cache/clang-format/ via pip if $PATH has a different version. Distro
# packages routinely differ from CI's, which is the whole reason for this.
resolve_clang_format() {
  if command -v clang-format >/dev/null 2>&1 &&
    clang-format --version | grep -qF "$CLANG_FORMAT_VERSION"; then
    command -v clang-format
    return
  fi

  local venv=".cache/clang-format/$CLANG_FORMAT_VERSION"
  if [[ ! -x "$venv/bin/clang-format" ]]; then
    echo "Provisioning clang-format $CLANG_FORMAT_VERSION into $venv" >&2
    python3 -m venv "$venv" >&2
    "$venv/bin/pip" install --quiet "clang-format==$CLANG_FORMAT_VERSION" >&2
  fi
  echo "$venv/bin/clang-format"
}

format_cxx() {
  local cf
  cf="$(resolve_clang_format)"
  echo "=== clang-format $("$cf" --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+') ($cf) ==="

  local find_args=()
  for pattern in "${CXX_EXCLUDES[@]}"; do
    find_args+=(-not -name "$pattern*")
  done

  local files=()
  while IFS= read -r -d '' f; do
    files+=("$f")
  done < <(find "${CXX_DIRS[@]}" -type f \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) \
    "${find_args[@]}" -print0)

  if [[ ${#files[@]} -eq 0 ]]; then
    echo "no C++ sources found in: ${CXX_DIRS[*]}" >&2
    return 1
  fi

  if [[ $CHECK -eq 1 ]]; then
    "$cf" --dry-run --Werror "${files[@]}"
  else
    "$cf" -i "${files[@]}"
    echo "formatted ${#files[@]} files"
  fi
}

# Resolve a dart matching DART_SDK_VERSION, downloading that SDK into
# .cache/dart-sdk/ if $PATH has a different one. Mirrors resolve_clang_format:
# a warning would tell you the versions diverged but still leave you unable to
# produce the formatting CI wants.
resolve_dart() {
  if command -v dart >/dev/null 2>&1 &&
    [[ "$(dart --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)" == "$DART_SDK_VERSION" ]]; then
    command -v dart
    return
  fi

  local root=".cache/dart-sdk/$DART_SDK_VERSION"
  if [[ ! -x "$root/dart-sdk/bin/dart" ]]; then
    echo "Provisioning Dart SDK $DART_SDK_VERSION into $root" >&2
    mkdir -p "$root"
    local url="https://storage.googleapis.com/dart-archive/channels/stable/release/$DART_SDK_VERSION/sdk/dartsdk-linux-x64-release.zip"
    curl -fsSL -o "$root/sdk.zip" "$url" >&2
    unzip -qo "$root/sdk.zip" -d "$root" >&2
    rm -f "$root/sdk.zip"
  fi
  echo "$root/dart-sdk/bin/dart"
}

format_dart() {
  local dart_bin
  dart_bin="$(resolve_dart)"
  echo "=== dart format $("$dart_bin" --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1) ($dart_bin) ==="

  if [[ $CHECK -eq 1 ]]; then
    "$dart_bin" format --output=none --set-exit-if-changed "${DART_PATHS[@]}"
  else
    "$dart_bin" format "${DART_PATHS[@]}"
  fi
}

# clang-tidy rewrites code and reports line numbers against the unformatted
# tree, so formatting must come after it, never before.
if [[ $DO_CXX -eq 1 ]]; then
  format_cxx
fi
if [[ $DO_DART -eq 1 ]]; then
  format_dart
fi
