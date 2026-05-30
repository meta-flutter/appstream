## 0.2.2

- Cross-compile support in the native asset build hook (`hook/build.dart`):
  when a `cmake_toolchain_file` and/or `cross_compile_env` user_define is
  supplied (e.g. by a Yocto/OE SDK image build), the hook drives
  `CMakeLists.txt` with the cross toolchain instead of `CBuilder`, re-exporting
  the toolchain environment that the Dart hooks runner otherwise strips. Host
  builds are unchanged. See [DEVELOPMENT.md](DEVELOPMENT.md).
- `skip_native_build` user_define makes the hook a no-op, for build systems
  (e.g. a bitbake recipe using `cmake.bbclass`) that compile `libappstream.so`
  themselves
- `CMakeLists.txt`: add `APPSTREAM_LIB_OUTPUT_DIR` (redirect the built
  `libappstream.so` out of the source tree) and `APPSTREAM_BUILD_TESTS`
  (skip the GoogleTest fetch/build for cross/SDK builds) options

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
