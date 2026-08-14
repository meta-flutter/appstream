// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

/// Native asset identifier for the appstream C++ library.
///
/// This file's library URI, `package:appstream_dart/src/appstream_native.dart`,
/// is the asset id. `hook/build.dart` emits the compiled `libappstream.so`
/// under that id, and `lib/src/bindings.dart` names it in its `@DefaultAsset`
/// annotation so its `@Native` externals resolve against it. The three must
/// agree exactly; a mismatch is not a build error, it is a symbol resolution
/// failure at the first native call.
///
/// The library is compiled automatically during `dart pub get` / `flutter
/// pub get` from the C++ sources in src/.
library;
