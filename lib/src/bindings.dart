// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

/// FFI bindings for libappstream.so.
///
/// Symbols are declared as `@Native` externals bound to the code asset that
/// `hook/build.dart` emits, so the Dart VM resolves them through its asset
/// table. That table is the only mechanism the VM consults for native
/// assets — a plain `DynamicLibrary.open('libappstream.so')` never sees it,
/// which is why this library previously had to guess where the built
/// artifact had landed.
///
/// The asset id below must stay identical to the one emitted by
/// `hook/build.dart` (package name + `src/appstream_native.dart`), or symbol
/// resolution fails at the first native call.
@DefaultAsset('package:appstream_dart/src/appstream_native.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

/// `int64_t appstream_init(void* data)`
@Native<Int64 Function(Pointer<Void>)>(symbol: 'appstream_init')
external int _appstreamInit(Pointer<Void> data);

/// `int64_t appstream_parse_to_sqlite(const char* xml_path,
///   const char* db_path, const char* language, int64_t dart_port,
///   int64_t batch_size)`
@Native<
  Int64 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int64, Int64)
>(symbol: 'appstream_parse_to_sqlite')
external int _appstreamParseToSqlite(
  Pointer<Utf8> xmlPath,
  Pointer<Utf8> dbPath,
  Pointer<Utf8> language,
  int dartPort,
  int batchSize,
);

/// `const char* appstream_version(void)`
@Native<Pointer<Utf8> Function()>(symbol: 'appstream_version')
external Pointer<Utf8> _appstreamVersion();

/// Resolved FFI bindings.
///
/// A thin facade over the `@Native` externals above. It carries no state:
/// the VM binds each symbol lazily on first call, so there is no library
/// handle to hold and no load ordering to get wrong.
class AppstreamBindings {
  const AppstreamBindings._();

  /// Returns the bindings.
  ///
  /// Kept as a factory for call-site compatibility. Nothing is loaded here —
  /// `@Native` symbols resolve against the asset table on first use, so a
  /// missing or unbuilt library surfaces at the first native call rather
  /// than here.
  factory AppstreamBindings.load() => const AppstreamBindings._();

  /// Initializes the Dart API DL protocol with [data].
  int init(Pointer<Void> data) => _appstreamInit(data);

  /// Parses [xmlPath] into the SQLite database at [dbPath].
  int parseToSqlite(
    Pointer<Utf8> xmlPath,
    Pointer<Utf8> dbPath,
    Pointer<Utf8> language,
    int dartPort,
    int batchSize,
  ) => _appstreamParseToSqlite(xmlPath, dbPath, language, dartPort, batchSize);

  /// Returns the native library version string.
  Pointer<Utf8> version() => _appstreamVersion();
}
