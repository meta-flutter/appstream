# AppStream to SQLite parser

A high-performance C++23 FFI bridge for parsing AppStream metadata into SQLite databases, with Dart bindings, a Drift ORM layer, multi-language translation support, and a Flutter example app.

## Quick start

```dart
import 'package:appstream_dart/appstream_dart.dart';

Future<void> main() async {
  // One-time native library init.
  Appstream.initialize();

  // Stream-parse an AppStream XML file into SQLite.
  await for (final event in Appstream.parseToSqlite(
    xmlPath: 'appstream.xml',
    dbPath: 'catalog.db',
    language: '*', // store every translation
  )) {
    switch (event) {
      case ComponentParsed(:final component):
        print('${component.id}: ${component.name}');
      case ParseDone(:final count):
        print('Done — $count components');
      case ParseFailed(:final message):
        throw StateError(message);
    }
  }

  // Query via the Drift ORM layer.
  final db = CatalogDatabase.open('catalog.db');
  final results = await db.searchComponents('calculator');
  for (final r in results) {
    print('${r.component.name}  (${r.component.id})');
  }
  await db.close();
}
```

Add to your `pubspec.yaml`:

```yaml
dependencies:
  appstream_dart: ^0.2.2
```

The first `dart pub get` triggers `hook/build.dart`, which drives CMake
to compile `libappstream.so` from the bundled C++23 sources. You'll
need a C++23 compiler (GCC 13+ or Clang 18+), CMake ≥ 3.22, and
`libsqlite3-dev`. See `doc/ADVANCED_BUILD.md` for sanitizer, coverage,
and benchmark configurations.

## Quick Facts

- **Language**: C++23 (backend) + Dart (frontend) + C (Dart API)
- **Status**: Production-Ready (v0.2.2)
- **Tests**: 185/185 passing (140 C++ + 45 Dart)
- **Peak Memory**: ~22 MB (streaming parser with 256 KB sliding buffer)

## Features

### Core Capabilities
- **Streaming XML Parsing** - XmlScanner with fd-based sliding buffer (~256 KB resident) for minimal memory footprint
- **Streaming Pipeline** - XML to SQLite direct pipeline via ComponentSink interface
- **Multi-Language Translations** - Stores per-field translations (name, summary, description) in a dedicated table; select language at runtime with locale fallback chain
- **Drift ORM Layer** - Type-safe query API with 20 tables, FTS5 full-text search, locale-aware queries, icon URL resolution, category/language browsing, and metrics
- **String Interning** - Efficient memory usage with StringPool for categories and keywords
- **Real-World Tested** - Parses the full Flathub catalog (~4500 components in ~260 ms)

### CLI Tools
- **bin/main.dart** - Downloads, decompresses, and parses AppStream XML to SQLite with progress bars
- **bin/query.dart** - Interactive query tool: search, detail, categories, languages, releases, metrics

### Flutter Example
- **example/flathub_catalog/** - Full Flutter Linux desktop app modeled after flathub.org with:
  - Setup screen with download/import progress (skipped if DB exists)
  - Catalog browsing with category sidebar and FTS5 search
  - Global language picker (auto-detects system locale, 327+ languages available)
  - Component detail with localized name/summary/description, HTML rendering, screenshot gallery with fullscreen viewer
  - Keyboard navigation (Escape to go back, arrow keys in image viewer)

### Infrastructure
- **Automated CI/CD** - GitHub Actions with 8+ configurations
- **Code Coverage** - gcov/lcov integration + Codecov
- **Memory Safety** - AddressSanitizer, UBSan support
- **Security Hardening** - URI scheme validation, FTS5 query sanitization, XML integrity checks, SQLITE_TRANSIENT bindings, numeric entity overflow protection
- **Comprehensive Tests** - Unit + integration + real-world data tests

## Project Structure

```
appstream/
├── src/                          # C++ source
│   ├── AppStreamParser.cpp       # XML parsing state machine + translation capture
│   ├── XmlScanner.cpp            # XML tokenizer (buffer + streaming fd modes)
│   ├── Component.cpp             # Component data model + FieldTranslation
│   ├── SqliteWriter.cpp          # Batched SQLite writer with staging
│   ├── StringPool.cpp            # String interning
│   ├── appstream_ffi.cpp         # Dart FFI bridge + DartNotifySink
│   └── dart_api_dl.c             # Dart API DL initialization
├── include/                      # C++ headers
├── lib/                          # Dart package
│   ├── appstream.dart            # Public API + exports
│   └── src/
│       ├── bindings.dart         # FFI bindings + library loading
│       └── database/
│           ├── database.dart     # CatalogDatabase (Drift ORM, locale-aware queries)
│           ├── tables.dart       # 20 Drift table definitions
│           └── database.g.dart   # Generated Drift code
├── bin/                          # CLI tools
│   ├── main.dart                 # Fetch + parse CLI with progress bars
│   └── query.dart                # Database query CLI
├── example/
│   └── flathub_catalog/          # Flutter example app
│       ├── lib/
│       │   ├── main.dart         # App entry point + ListenableBuilder
│       │   ├── services/         # CatalogService (download, import, locale, query)
│       │   ├── screens/          # SetupScreen, CatalogScreen, DetailScreen
│       │   └── widgets/          # AppCard, AppIcon
│       └── linux/                # Linux desktop build (bundles libappstream.so)
├── test/                         # Dart tests
├── native_tests/                        # C++ tests (GoogleTest)
├── doc/                         # Documentation
├── CMakeLists.txt                # Native build (driven by hook/build.dart)
└── pubspec.yaml                  # Dart dependencies
```

## Quick Start

### Prerequisites
- C++23 compatible compiler (GCC 13+, Clang 17+)
- CMake ≥ 3.22 (Ninja optional but recommended)
- Dart SDK 3.10+
- SQLite3 development libraries
- Flutter SDK (for example app)

### Build the Native Library

The native library is built automatically by `hook/build.dart` (via CMake)
the first time you run `dart pub get`, `dart run`, `dart test`, or
`flutter build`. No manual build step is required.

To build by hand for native development:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run the CLI

```bash
# Download and parse AppStream catalog to SQLite (defaults only)
dart run bin/main.dart

# Parse with all translations (327+ languages, ~50 MB DB)
dart run bin/main.dart --lang '*'

# Parse with specific languages
dart run bin/main.dart --lang 'en,de,fr,es,ja'

# Query the catalog
dart run bin/query.dart search firefox
dart run bin/query.dart detail org.mozilla.firefox
dart run bin/query.dart categories
dart run bin/query.dart metrics
```

### Run the Flutter Example

```bash
cd example/flathub_catalog
flutter pub get
flutter run -d linux
```

### Build & Test (Full)

The C++ suite is opt-in. Without `-DAPPSTREAM_BUILD_TESTS=ON` the test
targets are never configured and `ctest` reports no tests to run, so the
flag is required in every command below.

```bash
# Build with tests
cmake -S . -B build -DAPPSTREAM_BUILD_TESTS=ON && cmake --build build
ctest --test-dir build --output-on-failure

# With sanitizers
cmake -S . -B build -DAPPSTREAM_BUILD_TESTS=ON -DENABLE_SANITIZER=asan
cmake --build build && ctest --test-dir build --output-on-failure

# Dart tests
dart test

# Everything at once (C++ then Dart)
./scripts/test.sh
```

### Formatting

```bash
./scripts/format.sh            # apply formatting
./scripts/format.sh --check    # verify only, as CI does
```

Formatter output is version-sensitive: clang-format 18 and 22 disagree
about constructs this codebase uses, and Dart 3.13 collapses some call
arguments differently than 3.12. Running the formatter straight off your
`$PATH` can therefore produce a tree that passes locally and fails CI.

`scripts/format.sh` pins both tools and downloads a matching clang-format
and Dart SDK into `.cache/` when your installed versions differ, so it
reaches the same verdict as CI on any machine. CI runs this same script,
and the list of files to format lives in it rather than being duplicated
into the workflow.

Run `clang-tidy` *before* formatting, never after — it reports line numbers
against the unformatted tree.

## Multi-Language Support

The parser captures `xml:lang` variants of translatable fields and stores them in a `component_field_translations` table:

```sql
component_field_translations (component_id, field, language, value)
-- field: 'name', 'summary', 'description', 'developer_name', 'caption:N'
-- language: 'de', 'fr', 'pt-BR', 'zh-Hans-CN', etc.
```

### Language parameter

| Value                 | Behavior                             | DB Size   |
|-----------------------|--------------------------------------|-----------|
| `""` (empty, default) | Default values only, no translations | ~26 MB    |
| `"en,de,fr"`          | Default + specific languages         | ~30-35 MB |
| `"*"`                 | All 327+ languages                   | ~50 MB    |

### Runtime locale selection

```dart
final db = CatalogDatabase.open('catalog.db');

// Get translated name with fallback: pt-BR -> pt -> default
final name = await db.getTranslation('org.gnome.Calculator', 'name', 'pt-BR');

// List components with localized names
final apps = await db.listComponentsLocalized(locale: 'de', limit: 50);

// Filter to only components with German translations
final german = await db.componentsByTranslationLanguage('de', limit: 50);

// Categories filtered to a language
final cats = await db.listCategoriesForLanguage('de');
```

## Dart API Usage

```dart
import 'package:appstream/appstream.dart';

void main() async {
  Appstream.initialize();

  // Parse with all translations
  await for (final event in Appstream.parseToSqlite(
    xmlPath: 'appstream.xml',
    dbPath: 'catalog.db',
    language: '*',
  )) {
    switch (event) {
      case ComponentParsed(:final component):
        print('${component.id}: ${component.name}');
      case ParseDone(:final count):
        print('Done: $count components');
      case ParseFailed(:final message):
        print('Error: $message');
    }
  }

  // Query via Drift ORM
  final db = CatalogDatabase.open('catalog.db');
  final results = await db.searchComponents('firefox');
  final detail = await db.getComponentDetail('org.mozilla.firefox');
  final categories = await db.listCategories();
  final metrics = await db.getMetrics();
  final langs = await db.listTranslationLanguages();
  await db.close();
}
```

## Architecture

### Data Flow

```
AppStream XML (gzipped, ~7 MB)
    │ HTTP download + gzip decompress + integrity check
    ▼
appstream.xml (~42 MB on disk)
    │ open() + read() into 256 KB sliding buffer
    ▼
XmlScanner (pull parser, zero-copy string_views)
    │ START_ELEMENT / TEXT / END_ELEMENT events
    │ string_views valid until next next() call
    ▼
AppStreamParser (state machine)
    │ Component objects + FieldTranslation vectors
    │ Language set filter: "", "en,de", or "*"
    ▼
ComponentSink interface
    ├── DartNotifySink → Dart port + SqliteWriter
    ├── SqliteWriter → batched SQLITE_TRANSIENT inserts, staging + atomic rename
    └── InMemorySink → retains all components for queries
    ▼
catalog.db (SQLite, 20 tables + FTS5)
    │ Drift ORM with locale-aware queries
    ▼
CatalogDatabase
    ├── searchComponents / searchWithSnippets (FTS5, sanitized)
    ├── listComponentsLocalized (correlated subqueries)
    ├── componentsByTranslationLanguage (EXISTS filter)
    ├── getTranslation (locale fallback chain)
    └── getMetrics
```

### Database Schema

20 normalized tables with interned lookups:

| Table                                                              | Purpose                                                                       |
|--------------------------------------------------------------------|-------------------------------------------------------------------------------|
| `components`                                                       | Core app metadata (id, type, name, summary, description, licenses, developer) |
| `categories` / `component_categories`                              | Interned category names + junction                                            |
| `keywords` / `component_keywords`                                  | Interned keyword names + junction                                             |
| `component_urls`                                                   | URLs by type (homepage, bugtracker, donation, etc.)                           |
| `component_icons`                                                  | Icons by type (stock, cached, remote) with dimensions                         |
| `releases` / `release_issues`                                      | Release versions, dates, descriptions, CVEs                                   |
| `screenshots` / `screenshot_images` / `screenshot_videos`          | Screenshot gallery                                                            |
| `content_rating_attrs`                                             | OARS content ratings                                                          |
| `component_languages`                                              | Supported languages                                                           |
| `branding_colors`                                                  | Light/dark scheme colors                                                      |
| `component_extends` / `component_suggests` / `component_relations` | Cross-references                                                              |
| `component_custom`                                                 | Custom key-value metadata                                                     |
| `component_field_translations`                                     | Localized field values (name, summary, description per language)              |
| `components_fts`                                                   | FTS5 full-text search index                                                   |

## Performance

| Metric                                | Value              |
|---------------------------------------|--------------------|
| Full catalog parse (defaults only)    | ~260 ms, ~26 MB DB |
| Full catalog parse (all translations) | ~350 ms, ~50 MB DB |
| Peak memory (streaming)               | ~22 MB             |
| FTS search                            | < 5 ms             |

## Documentation

| Document                    | Purpose                                  |
|-----------------------------|------------------------------------------|
| `doc/ARCHITECTURE.md`      | System architecture and design decisions |
| `doc/ADVANCED_BUILD.md`    | Build configuration guide                |
| `doc/RUNNING_TESTS.md`     | Test execution and debugging             |
| `doc/CODE_AUDIT_REPORT.md` | Security and code quality audit          |

## License

Apache License 2.0 - See LICENSE file

## Contributors

- Joel Winarske (Creator & Maintainer)
