// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
//
// Native assets build hook for appstream_dart.
//
// Drives the project's CMake build to compile libappstream.so, then
// declares the resulting shared library as a CodeAsset under the asset id
// `package:appstream_dart/src/appstream_native.dart`. The @Native externals
// in lib/src/bindings.dart name that same id in their @DefaultAsset, which
// is what lets the VM resolve their symbols. Changing the asset name below
// without changing that annotation breaks loading at the first call.
//
// Patterned after https://github.com/jwinarske/pw_dart/blob/main/hook/build.dart

import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    // Allow the embedder/build system to suppress the in-hook native build via
    // a hooks user-define (e.g. an OpenEmbedded/Yocto recipe that builds
    // libappstream.so itself with the cross toolchain). Dart runs hooks in a
    // hermetic environment, so an environment variable would not reach here;
    // user-defines from the workspace pubspec.yaml are passed to hooks.
    if (input.userDefines['skip_native_build'] == true) {
      stderr.writeln(
        'skip_native_build user-define set — skipping native build.',
      );
      return;
    }

    final pkgRoot = input.packageRoot.toFilePath();
    final buildDir = input.outputDirectory.resolve('cmake/').toFilePath();

    await Directory(buildDir).create(recursive: true);

    final hasNinja = await _which('ninja');

    if (!File('${buildDir}CMakeCache.txt').existsSync()) {
      await _run('cmake', [
        '-S',
        pkgRoot,
        '-B',
        buildDir,
        '-DCMAKE_BUILD_TYPE=Release',
        // APPSTREAM_BUILD_TESTS, not BUILD_TESTING: the latter is not a
        // variable this project defines, so CMake ignored it and warned
        // "Manually-specified variables were not used by the project" on
        // every Flutter build. The canonical flag list lives in
        // scripts/configure.sh; this hook cannot call it, because build
        // hooks run in a hermetic environment without a shell contract.
        '-DAPPSTREAM_BUILD_TESTS=OFF',
        '-DAPPSTREAM_HOOK_BUILD=ON',
        if (hasNinja) ...['-G', 'Ninja'],
      ]);
    }

    await _run('cmake', ['--build', buildDir, '--parallel']);

    // CMake's default LIBRARY_OUTPUT_DIRECTORY is the build dir root.
    final libFile = File('${buildDir}libappstream.so');
    if (!libFile.existsSync()) {
      throw StateError('libappstream.so not found at ${libFile.path}');
    }

    output.assets.code.add(
      CodeAsset(
        package: input.packageName,
        name: 'src/appstream_native.dart',
        linkMode: DynamicLoadingBundled(),
        file: libFile.uri,
      ),
    );

    // Re-run the hook whenever any C/C++ source or CMake file changes.
    for (final dir in ['src', 'include']) {
      final d = Directory('$pkgRoot/$dir');
      if (!d.existsSync()) continue;
      for (final entity in d.listSync(recursive: true)) {
        if (entity is! File) continue;
        final p = entity.path;
        if (p.endsWith('.cpp') ||
            p.endsWith('.cc') ||
            p.endsWith('.c') ||
            p.endsWith('.hpp') ||
            p.endsWith('.h')) {
          output.dependencies.add(entity.uri);
        }
      }
    }
    output.dependencies.add(Uri.file('$pkgRoot/CMakeLists.txt'));

    stderr.writeln('libappstream built: ${libFile.path}');
  });
}

Future<void> _run(String exe, List<String> args) async {
  final p = await Process.start(exe, args, mode: ProcessStartMode.inheritStdio);
  final code = await p.exitCode;
  if (code != 0) {
    throw ProcessException(exe, args, 'exit code $code', code);
  }
}

Future<bool> _which(String exe) async {
  final r = await Process.run('which', [exe]);
  return r.exitCode == 0;
}
