// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

import 'dart:io';

import 'package:appstream_dart/appstream.dart';
import 'package:test/test.dart';

/// Eagerly initialize the native library so the result is available
/// when tests are registered (skip: is evaluated at registration time,
/// not at run time).
bool _initNative() {
  try {
    Appstream.initialize();
    return true;
  } catch (_) {
    return false;
  }
}

final _nativeReady = _initNative();

void main() {
  test('parseToSqlite falls back when isolate spawn fails', () async {
    final tempDir = await Directory.systemTemp.createTemp('appstream_test_');
    final xmlPath = '${tempDir.path}/appstream.xml';
    final dbPath = '${tempDir.path}/catalog.db';

    const xml = '''<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Test</id>
    <name>Test App</name>
    <summary>Integration test component</summary>
  </component>
</components>
''';

    try {
      await File(xmlPath).writeAsString(xml);

      final events = await Appstream.parseToSqlite(
        xmlPath: xmlPath,
        dbPath: dbPath,
        workerSpawner:
            (
              void Function(Map<String, Object>) _,
              Map<String, Object> _,
            ) async {
              throw StateError('forced spawn failure');
            },
      ).toList().timeout(const Duration(seconds: 30));

      final failures = events.whereType<ParseFailed>().toList();
      expect(
        failures,
        isEmpty,
        reason: 'Fallback worker should complete parse without failures',
      );

      final done = events.whereType<ParseDone>().toList();
      expect(done, hasLength(1));
      expect(done.single.count, greaterThanOrEqualTo(1));

      expect(File(dbPath).existsSync(), isTrue);
      expect(File(dbPath).lengthSync(), greaterThan(0));
    } finally {
      if (tempDir.existsSync()) {
        await tempDir.delete(recursive: true);
      }
    }
  }, skip: _nativeReady ? null : 'native library not available');

  test(
    'parseToSqlite emits ParseFailed or empty ParseDone on loosely-malformed XML',
    () async {
      final tempDir = await Directory.systemTemp.createTemp(
        'appstream_test_bad_',
      );
      final xmlPath = '${tempDir.path}/bad.xml';
      final dbPath = '${tempDir.path}/catalog.db';

      try {
        // The scanner is lenient with some malformed input and may produce 0
        // components rather than an error.  Either outcome is acceptable; what
        // must NOT happen is an unhandled exception escaping the stream.
        await File(xmlPath).writeAsString('<<not valid xml>>');

        final events = await Appstream.parseToSqlite(
          xmlPath: xmlPath,
          dbPath: dbPath,
        ).toList().timeout(const Duration(seconds: 30));

        final failures = events.whereType<ParseFailed>().toList();
        final done = events.whereType<ParseDone>().toList();
        expect(
          failures.isNotEmpty || (done.isNotEmpty && done.single.count == 0),
          isTrue,
          reason: 'Malformed XML should produce ParseFailed or empty ParseDone',
        );

        if (failures.isNotEmpty) {
          expect(failures.first.message, isNotEmpty);
        }
      } finally {
        if (tempDir.existsSync()) {
          await tempDir.delete(recursive: true);
        }
      }
    },
    skip: _nativeReady ? null : 'native library not available',
  );

  test('parseToSqlite emits ParseFailed with "Native parser error:" prefix on '
      'unquoted XML attribute (MALFORMED_TAG)', () async {
    final tempDir = await Directory.systemTemp.createTemp(
      'appstream_test_malformed_',
    );
    final xmlPath = '${tempDir.path}/malformed.xml';
    final dbPath = '${tempDir.path}/catalog.db';

    try {
      // An unquoted attribute value is a hard error in XmlScanner
      // (MALFORMED_TAG).  The C++ sends "ERROR\tParse failed (code N)" over
      // the Dart port, which the Dart layer wraps as
      // ParseFailed('Native parser error: Parse failed (code N)').
      const xml =
          '<?xml version="1.0" encoding="UTF-8"?>\n'
          '<components>'
          '<component type=desktop-application>' // ← unquoted value
          '<id>com.example.Bad</id>'
          '</component>'
          '</components>';

      await File(xmlPath).writeAsString(xml);

      final events = await Appstream.parseToSqlite(
        xmlPath: xmlPath,
        dbPath: dbPath,
      ).toList().timeout(const Duration(seconds: 30));

      // Must get exactly one ParseFailed and no ParseDone.
      final failures = events.whereType<ParseFailed>().toList();
      final done = events.whereType<ParseDone>().toList();

      expect(
        failures,
        isNotEmpty,
        reason: 'Unquoted attribute must produce ParseFailed',
      );
      expect(
        done,
        isEmpty,
        reason: 'A scanner error must not produce a ParseDone',
      );

      // The message must use the canonical "Native parser error: " prefix
      // that the Dart layer adds when it receives an ERROR tab-message
      // from C++.
      expect(
        failures.first.message,
        startsWith('Native parser error: '),
        reason: 'Message shape must be "Native parser error: ..."',
      );

      // The C++ embeds the numeric error code — confirm it is present.
      expect(
        failures.first.message,
        contains('Parse failed (code '),
        reason: 'C++ error payload must include the numeric code',
      );
    } finally {
      if (tempDir.existsSync()) {
        await tempDir.delete(recursive: true);
      }
    }
  }, skip: _nativeReady ? null : 'native library not available');
}
