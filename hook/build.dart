import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

/// Native asset id for libappstream.so. Kept identical across both build
/// paths below so the produced asset is interchangeable.
const String _assetName = 'package:appstream/src/appstream_native.dart';

void main(List<String> args) => build(args, _build);

/// Build the appstream native library from C++ source.
///
/// Three paths:
///   * Skip — when the `skip_native_build` user_define is set, do nothing.
///     Used by build systems that compile libappstream.so themselves (e.g.
///     a bitbake recipe driving the project's CMakeLists.txt directly) and
///     only need the Dart side here.
///   * Host / local-dev — `native_toolchain_c`'s [CBuilder] compiles the
///     sources directly with the auto-detected host toolchain.
///   * Cross-compile — when an embedding build system (e.g. an SDK-driven
///     image build) supplies a toolchain via `user_defines`, drive the
///     project's CMakeLists.txt instead so the cross toolchain, sysroot,
///     and flags are honored. See [_buildWithCMake] for why this path
///     exists.
///
/// Requires a C++23 compiler:
/// - GCC 13+
/// - Clang 18+ (std::expected polyfilled when not available)
///
/// Also requires SQLite3 development libraries.
Future<void> _build(BuildInput input, BuildOutputBuilder output) async {
  if (!input.config.buildCodeAssets) return;

  if (input.userDefines['skip_native_build'] != null) {
    stderr.writeln('skip_native_build user_define set — skipping native build.');
    return;
  }

  final cmakeToolchainFile =
      input.userDefines['cmake_toolchain_file'] as String?;
  final crossEnv = _readCrossEnv(input);

  if (cmakeToolchainFile != null || crossEnv.isNotEmpty) {
    await _buildWithCMake(input, output, cmakeToolchainFile, crossEnv);
  } else {
    await _buildWithCBuilder(input, output);
  }
}

/// Host build: let CBuilder pick the toolchain and compile the sources.
Future<void> _buildWithCBuilder(
  BuildInput input,
  BuildOutputBuilder output,
) async {
  final builder = CBuilder.library(
    name: 'appstream',
    assetName: _assetName,
    sources: [
      'src/dart_api_dl.cpp',
      'src/appstream_ffi.cpp',
      'src/AppStreamParser.cpp',
      'src/Component.cpp',
      'src/XmlScanner.cpp',
      'src/SqliteWriter.cpp',
      'src/StringPool.cpp',
    ],
    includes: ['include'],
    libraries: ['sqlite3', 'pthread'],
    language: Language.cpp,
    std: 'c++23',
    cppLinkStdLib: 'stdc++',
    flags: [
      '-fvisibility=hidden',
    ],
  );

  await builder.run(input: input, output: output);
}

/// Cross-compile build: drive CMakeLists.txt with the supplied toolchain.
///
/// The Dart hooks runner executes hooks under a semi-hermetic environment
/// allow-list (https://dart.dev/tools/hooks#environment-variables). Toolchain
/// variables exported in the parent shell — CC, CXX, CFLAGS, CXXFLAGS,
/// --sysroot, OECORE_TARGET_SYSROOT, OECORE_TARGET_ARCH,
/// OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX, PKG_CONFIG_*, etc. — are stripped
/// before this process starts, and CBuilder exposes no channel to put them
/// back (it resolves the compiler from the launcher's `cCompiler` config or
/// host auto-detection and offers no environment override). So they get
/// silently dropped on a cross build.
///
/// The transport that survives the boundary is `user_defines`: they cross the
/// hermetic boundary and participate in cache-key invalidation (a change to
/// any value re-runs the build, which is what we want). The embedding build
/// system passes:
///
///   hooks:
///     user_defines:
///       appstream_dart:
///         cmake_toolchain_file: <path>   # -DCMAKE_TOOLCHAIN_FILE=...
///         cross_compile_env:             # map re-exported into cmake
///           CC: aarch64-poky-linux-gcc ... --sysroot=...
///           CXX: aarch64-poky-linux-g++ ... --sysroot=...
///           CFLAGS: " -O2 -g -pipe ..."
///           CXXFLAGS: " -O2 -g -pipe ..."
///           OECORE_TARGET_SYSROOT: /path/to/sysroots/...
///           OECORE_TARGET_ARCH: aarch64
///           OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX: ""
///           # plus PKG_CONFIG_*, LD/AR/RANLIB/STRIP, etc. as needed
///
/// A generated toolchain file (e.g. OEToolchainConfig.cmake) reads CFLAGS /
/// CXXFLAGS / OECORE_TARGET_SYSROOT / OECORE_TARGET_ARCH /
/// OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX directly via $ENV{...} at configure
/// time, so passing only the toolchain file is not enough — the env map has
/// to be re-exported into the cmake subprocess, which is what we do here.
Future<void> _buildWithCMake(
  BuildInput input,
  BuildOutputBuilder output,
  String? cmakeToolchainFile,
  Map<String, String> crossEnv,
) async {
  final String pkgRoot = input.packageRoot.toFilePath();
  final String buildDir = input.outputDirectory.resolve('cmake/').toFilePath();
  final String libOutDir = input.outputDirectory.resolve('lib/').toFilePath();

  await Directory(buildDir).create(recursive: true);
  await Directory(libOutDir).create(recursive: true);

  final bool hasNinja = await _which('ninja');

  if (!File('${buildDir}CMakeCache.txt').existsSync()) {
    await _run('cmake', [
      '-S', pkgRoot,
      '-B', buildDir,
      '-DCMAKE_BUILD_TYPE=Release',
      '-DAPPSTREAM_BUILD_TESTS=OFF',
      '-DAPPSTREAM_LIB_OUTPUT_DIR=$libOutDir',
      if (cmakeToolchainFile != null)
        '-DCMAKE_TOOLCHAIN_FILE=$cmakeToolchainFile',
      if (hasNinja) ...['-G', 'Ninja'],
    ], env: crossEnv);
  }

  await _run('cmake', ['--build', buildDir, '--parallel'], env: crossEnv);

  final libFile = File('${libOutDir}libappstream.so');
  if (!libFile.existsSync()) {
    throw StateError('libappstream.so not found at ${libFile.path}');
  }

  output.assets.code.add(
    CodeAsset(
      package: input.packageName,
      name: _assetName,
      linkMode: DynamicLoadingBundled(),
      file: libFile.uri,
    ),
  );

  // Re-run the hook whenever any source / header / cmake file changes.
  for (final dir in ['src', 'include']) {
    final d = Directory('$pkgRoot$dir');
    if (!d.existsSync()) continue;
    for (final FileSystemEntity entity in d.listSync(recursive: true)) {
      if (entity is! File) continue;
      final String p = entity.path;
      if (p.endsWith('.cpp') ||
          p.endsWith('.cc') ||
          p.endsWith('.c') ||
          p.endsWith('.hpp') ||
          p.endsWith('.h')) {
        output.dependencies.add(entity.uri);
      }
    }
  }
  output.dependencies.add(Uri.file('${pkgRoot}CMakeLists.txt'));
}

/// Reads the `cross_compile_env` user_define into a `{String: String}` map.
/// Empty when unset (the host-build case).
Map<String, String> _readCrossEnv(BuildInput input) {
  final Map<String, String> env = {};
  final Object? def = input.userDefines['cross_compile_env'];
  if (def is Map) {
    for (final MapEntry<dynamic, dynamic> entry in def.entries) {
      env[entry.key.toString()] = entry.value.toString();
    }
  } else if (def != null) {
    throw StateError(
      'cross_compile_env userDefine must be a map of {String: String}; '
      'got ${def.runtimeType}.',
    );
  }
  return env;
}

Future<void> _run(
  String exe,
  List<String> args, {
  Map<String, String>? env,
}) async {
  // includeParentEnvironment stays true so PATH/HOME/TMPDIR/HTTP_PROXY etc.
  // from the hooks-runner allow-list still flow through; `env` (the
  // cross_compile_env map, empty for host builds) layers on top.
  final Process p = await Process.start(
    exe,
    args,
    mode: ProcessStartMode.inheritStdio,
    environment: env == null || env.isEmpty ? null : env,
    includeParentEnvironment: true,
  );
  final int code = await p.exitCode;
  if (code != 0) {
    throw ProcessException(exe, args, 'exit code $code', code);
  }
}

Future<bool> _which(String exe) async {
  final ProcessResult r = await Process.run('which', [exe]);
  return r.exitCode == 0;
}