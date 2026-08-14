## 0.4.1

- Restore Flutter compatibility, which 0.4.0 broke. `hooks` ^2.1.0 requires
  `meta` ^1.19.0, but `flutter_test` from the Flutter SDK pins `meta` 1.18.0
  on 3.44.x, so *any* Flutter app failed version solving against
  appstream_dart 0.4.0 — the bundled example included. The constraints are
  now `hooks: '>=1.0.2 <3.0.0'` and `code_assets: '>=1.0.0 <2.0.0'`, which
  resolve to 2.1.0/1.2.1 standalone and to 1.0.2/1.0.0 under an older
  Flutter. The build hook is source-compatible with both majors.
- Bind the native symbols as `@Native` externals against `@DefaultAsset`
  instead of resolving them through `DynamicLibrary.open`. `hook/build.dart`
  already emitted the library as a code asset, but the VM consults its asset
  table only for `@Native` declarations, so that asset was built and then
  never used; the loader compensated with a seven-step search. The public
  API is unchanged.
- Remove that search chain (~180 lines): a `/proc/self/maps` scan, a glob
  through `.dart_tool/hooks_runner/` internals, and candidate paths derived
  from `Platform.script`, the executable, and the current directory. The
  last of those made the process load `libappstream.so` from a
  CWD-relative `lib/`, `build/`, or `src/build/` directory, so running an
  application from a directory an attacker could write to was enough to get
  a library of their choosing loaded.
- Correct the comments in `hook/build.dart` and `lib/src/appstream_native.dart`,
  which described the `@Native` mechanism that did not yet exist.
- Refresh stale facts in the README, which is the pub.dev landing page: the
  install snippet advertised `^0.2.2`, the status line and test count were
  three releases old (194 tests, not 185), the project tree was rooted at
  the pre-rename `appstream/` and listed a `dart_api_dl.c` that is now
  `.cpp`, and the prerequisites claimed Clang 17+ while the `std::expected`
  polyfill targets Clang 18.

## 0.4.0

- **Breaking (dependency resolution):** `hooks` ^1.0.2 → ^2.1.0 and
  `code_assets` ^1.0.0 → ^1.2.1. Consumers pinned to `hooks` 1.x will no
  longer resolve. The build hook API is unchanged between `hooks` 1.x and
  2.x, so `hook/build.dart` needed no edits and the public Dart API is
  untouched; the bump also unpins `native_toolchain_c` and `record_use`
  from their 1.x-era versions. The SDK constraint stays `^3.10.0`.
- Repository moved to `github.com/flatpak-minimal/appstream_dart`;
  `repository` and `issue_tracker` updated to match.
- Fix `scripts/test.sh` passing `-DBUILD_TESTING=ON`, which the CMake build
  ignores — the gate has been `-DAPPSTREAM_BUILD_TESTS=ON` since 0.2.2. The
  C++ suite was therefore never configured or rebuilt, and `ctest` silently
  ran whatever stale binary was left in the build directory. CI already
  passed the correct flag, so only local runs were affected.
- clang-tidy cleanups in `AppStreamParser` and `XmlScanner`: explicit
  parentheses in mixed `*`/`+` accumulator arithmetic, `contains()` in place
  of a `find() != npos` membership test, and consistent braces across the
  `provides` if/else chain.

## 0.3.0

- Licensing: adopt SPDX license headers (`SPDX-License-Identifier` /
  `SPDX-FileCopyrightText`) across all source files; add
  `THIRD_PARTY_LICENSES` cataloging every direct dependency.
- LICENSE file replaced with the compact SPDX-standard Apache-2.0 text.
- Dependency bumps: `sqlite3` ^2.4.0 → ^3.3.1, `lints` ^4.0.0 → ^6.1.0
  (applies to both the main package and the Flutter example).
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
