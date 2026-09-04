// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

#include "appstream_ffi.h"
#include "dart_api_dl.h"

#include "AppStreamParser.h"
#include "SqliteWriter.h"
#include "spdlog.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// ============================================================
// Post helpers — send strings to Dart via Channel B pattern
// ============================================================

/// Posts a string to the Dart port as kExternalTypedData (Uint8).
/// The string data is malloc'd and freed by Dart GC via the finalizer.
///
/// Returns true on success, false on failure (Dart API not initialized,
/// invalid port, or out-of-memory). Callers that need to surface OOM to
/// Dart should fall back to a stack-allocated sentinel post via
/// postOomSentinel().
static bool postString(const Dart_Port port, const std::string &msg) {
  if (!Dart_PostCObject_DL || port == ILLEGAL_PORT)
    return false;

  const auto len = msg.size();
  auto *buf = static_cast<uint8_t *>(malloc(len));
  if (!buf) {
    spdlog::error("postString: malloc({}) failed; dropping message", len);
    return false;
  }
  // The buffer is posted as kExternalTypedData/Uint8 below, consumed by Dart
  // as a length-prefixed byte view (never as a C string), so a trailing NUL
  // is intentionally not added.
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
  memcpy(buf, msg.data(), len);

  Dart_CObject obj{};
  obj.type = Dart_CObject_kExternalTypedData;
  obj.value.as_external_typed_data.type = Dart_TypedData_kUint8;
  obj.value.as_external_typed_data.length = static_cast<intptr_t>(len);
  obj.value.as_external_typed_data.data = buf;
  obj.value.as_external_typed_data.peer = buf;
  obj.value.as_external_typed_data.callback = [](void * /*isolate_data*/, void *peer) {
    free(peer);
  };

  Dart_PostCObject_DL(port, &obj);
  return true;
}

/// Posts an integer sentinel to Dart that the higher layer interprets as
/// "native side ran out of memory and could not deliver a normal message".
/// Uses Dart_CObject_kInt32 with a fixed reserved value so no allocation
/// is required on the failing path.
///
/// Sentinel value: -2 (matches the Dart-side OOM constant; see
/// lib/src/bindings.dart). -1 is reserved for generic parse errors.
static constexpr int32_t kOomSentinel = -2;

static void postOomSentinel(const Dart_Port port) {
  if (!Dart_PostCObject_DL || port == ILLEGAL_PORT)
    return;
  Dart_CObject obj{};
  obj.type = Dart_CObject_kInt32;
  obj.value.as_int32 = kOomSentinel;
  Dart_PostCObject_DL(port, &obj);
}

// ============================================================
// DartNotifySink — wraps SqliteWriter AND posts to Dart
// ============================================================

/// A ComponentSink that delegates to SqliteWriter for persistence
/// and posts each component's id + name + summary to a Dart port.
class DartNotifySink final : public ComponentSink {
public:
  DartNotifySink(SqliteWriter &writer, Dart_Port port) : writer_(writer), port_(port) {}

  std::expected<void, Error> begin() override { return writer_.begin(); }

  std::expected<void, Error> onComponent(Component component) override {
    // Build a tab-delimited message: "id\tname\tsummary"
    std::string msg;
    msg.reserve(component.id.size() + component.name.size() + component.summary.size() + 2);
    msg.append(component.id);
    msg.push_back('\t');
    msg.append(component.name);
    msg.push_back('\t');
    msg.append(component.summary);

    // Post to Dart (non-blocking, thread-safe). On allocation failure
    // surface a sentinel so the Dart side can distinguish a dropped
    // progress message from a successfully delivered one.
    if (!postString(port_, msg)) {
      postOomSentinel(port_);
    }

    // Forward to SQLite writer
    return writer_.onComponent(std::move(component));
  }

  std::expected<void, Error> end() override {
    auto r = writer_.end();

    if (r) {
      // Notify Dart after database is finalized (indices, FTS, rename)
      const std::string done = "DONE\t" + std::to_string(writer_.componentCount());
      postString(port_, done);
    }

    return r;
  }

  [[nodiscard]] size_t componentCount() const override { return writer_.componentCount(); }

private:
  SqliteWriter &writer_;
  Dart_Port port_;
};

// ============================================================
// Exported C ABI functions
// ============================================================

extern "C" {

int64_t appstream_init(void *data) {
  return Dart_InitializeApiDL(data);
}

int64_t appstream_parse_to_sqlite(const char *xml_path, const char *db_path, const char *language,
                                  int64_t dart_port, int64_t batch_size) {

  const auto port = static_cast<Dart_Port>(dart_port);

  SqliteWriter writer(db_path, static_cast<size_t>(batch_size));
  DartNotifySink sink(writer, port);

  auto result = AppStreamParser::parseToSink(xml_path, language ? language : "", sink);

  if (!result) {
    const std::string err =
        "ERROR\tParse failed (code " + std::to_string(static_cast<int>(result.error())) + ")";
    postString(port, err);
    return -1;
  }

  return 0;
}

const char *appstream_version(void) {
  return "appstream_parser v0.2.0 (C++23, Dart API DL 2)";
}

} // extern "C"
