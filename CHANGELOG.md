## 0.3.0

- Licensing: adopt SPDX license headers (`SPDX-License-Identifier` /
  `SPDX-FileCopyrightText`) across all source files; add
  `THIRD_PARTY_LICENSES` cataloging every direct dependency.
- LICENSE file replaced with the compact SPDX-standard Apache-2.0 text.
- Dependency bumps: `sqlite3` ^2.4.0 → ^3.3.1, `lints` ^4.0.0 → ^6.1.0
  (applies to both the main package and the Flutter example),
  `hooks` ^1.0.2 → ^2.1.0 and `code_assets` ^1.0.0 → ^1.2.1. The build
  hook API is unchanged between `hooks` 1.x and 2.x, so `hook/build.dart`
  needed no edits; the bump also unpins `native_toolchain_c` and
  `record_use` from their 1.x-era versions.
- Repository moved to `github.com/flatpak-minimal/appstream_dart`;
  `repository` and `issue_tracker` updated to match.
- Fix `scripts/test.sh` passing `-DBUILD_TESTING=ON`, which the CMake build
  ignores — the gate has been `-DAPPSTREAM_BUILD_TESTS=ON` since 0.2.2. The
  C++ suite was therefore never configured or rebuilt, and `ctest` silently
  ran whatever stale binary was left in the build directory.
- clang-tidy cleanups in `AppStreamParser` and `XmlScanner`: explicit
  parentheses in mixed `*`/`+` accumulator arithmetic, `contains()` in place
  of a `find() != npos` membership test, and consistent braces across the
  `provides` if/else chain.
- Public API documentation: add dartdoc comments to all exported classes,
  fields, and constructors in `lib/appstream.dart`,
  `lib/src/database/database.dart`, and `lib/src/database/tables.dart`.

## 0.2.2

- pub.dev publishing hygiene:
  - Add `lib/appstream_dart.dart` re-export so the primary library name
    matches the package name. The original `lib/appstream.dart` import
    continues to work.
  - Rename `docs/` → `doc/` and `tests/` → `native_tests/` to match the
    pub package layout (singular `doc/`, no clash with the Dart `test/`
    directory).
  - Add `.pubignore` to keep build artifacts, the cached
    `appstream.xml`/`catalog.db`, the Flutter example sub-package, and
    legacy/dev shell scripts out of the published archive.
- Native build: gate the C++ test suite behind
  `-DAPPSTREAM_BUILD_TESTS=ON` so the `package:hooks` build hook and
  downstream consumers no longer fetch GoogleTest or build the test
  executable by default.
- Reliability and security fixes surfaced by clang-tidy:
  - Fix 8 use-after-move bugs in `AppStreamParser` (member key strings
    were re-checked via `.empty()` after being moved).
  - Mark `SqliteWriter::~SqliteWriter` `noexcept` and wrap its body in a
    try/catch so a logging failure during teardown can no longer
    `std::terminate` the parsing process.
  - `postString` (FFI) now returns success/failure and a stack-allocated
    OOM sentinel (`-2`) is posted if the malloc fails, instead of the
    progress message being silently dropped.
  - Document path-handling expectations on `appstream_parse_to_sqlite`:
    paths are passed directly to `open(2)`/SQLite with no normalization
    or sandboxing, so callers accepting them from untrusted input must
    validate first.
- Tooling: add `.clang-format` and `.clang-tidy` at the repo root so
  formatting and lint runs are deterministic.
- Flutter example (`example/flathub_catalog`): drive the package's
  CMake build via `ExternalProject_Add` so `libappstream.so` is always
  built and bundled before the runner is linked.

## 0.2.1

- Add `std::expected` polyfill for Clang 18 (Flutter's default Linux
  toolchain), removing the hard requirement on Clang 19+
- Rename package from `appstream` to `appstream_dart` and fix example
  imports to match

## 0.2.0

- Multi-language translation support: store `xml:lang` field translations
  in `component_field_translations` table, select at runtime with locale
  fallback chain
- Streaming XML parser: replace mmap with fd-based 256 KB sliding buffer,
  reducing peak memory from ~64 MB to ~22 MB
- Drift ORM database layer with 20 tables, FTS5 search, locale-aware queries
- Native asset build hook (`hook/build.dart`) for automatic C++ compilation
- Flutter example app with catalog browsing, language picker, screenshot
  viewer, and AppStream HTML rendering
- Security hardening: SQLITE_TRANSIENT bindings, URI scheme validation,
  FTS5 query sanitization, numeric entity overflow protection

## 0.1.0

- Initial release
- C++23 XML parser with FFI bridge
- Streaming pipeline: XML to SQLite via ComponentSink
- Dart API with isolate-based parsing and progress events
- CLI tools for downloading, parsing, and querying Flathub catalog
