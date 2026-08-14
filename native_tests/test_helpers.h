// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
/// Shared helpers for appstream unit tests.
///
/// Included directly by each test_*.cpp — no separate compilation unit.
#pragma once

#include "expected_polyfill.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Component.h"
#include "ComponentSink.h"

// ── TempFile ──────────────────────────────────────────────────────────────
/// RAII helper: writes content to a unique temp file; deletes it (and any
/// related SQLite journal / staging files) on destruction.
class TempFile {
public:
  explicit TempFile(const std::string &content, const std::string &ext = ".xml") {
    static std::atomic<int> counter{0};
    path_ = (std::filesystem::temp_directory_path() /
             ("appstream_test_" + std::to_string(++counter) + ext))
                .string();
    std::ofstream f(path_, std::ios::binary);
    f << content;
    f.flush();
  }
  ~TempFile() { cleanup(path_); }

  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;

  [[nodiscard]] const std::string &str() const noexcept { return path_; }

private:
  std::string path_;

  static void cleanup(const std::string &base) {
    for (const char *s : {"", ".staging", ".staging-wal", ".staging-shm", "-wal", "-shm"}) {
      std::error_code ec;
      std::filesystem::remove(base + s, ec);
    }
  }
};

// ── TempPath ──────────────────────────────────────────────────────────────
/// RAII helper for a path that does not yet exist; used as output DB path.
class TempPath {
public:
  explicit TempPath(const std::string &ext = ".db") {
    static std::atomic<int> counter{0};
    path_ = (std::filesystem::temp_directory_path() /
             ("appstream_testdb_" + std::to_string(++counter) + ext))
                .string();
  }
  ~TempPath() {
    for (const char *s : {"", ".staging", ".staging-wal", ".staging-shm", "-wal", "-shm"}) {
      std::error_code ec;
      std::filesystem::remove(path_ + s, ec);
    }
  }

  TempPath(const TempPath &) = delete;
  TempPath &operator=(const TempPath &) = delete;

  [[nodiscard]] const std::string &str() const noexcept { return path_; }
  [[nodiscard]] bool exists() const { return std::filesystem::exists(path_); }

private:
  std::string path_;
};

// ── VectorSink ────────────────────────────────────────────────────────────
/// Collects every component delivered by the parser into a std::vector.
/// Set failOnComponent = true to simulate a DATABASE_ERROR on the first call.
struct VectorSink : ComponentSink {
  std::vector<Component> components;
  bool failOnComponent = false;

  std::expected<void, Error> onComponent(Component c) override {
    if (failOnComponent)
      return std::unexpected(Error::DATABASE_ERROR);
    components.push_back(std::move(c));
    return {};
  }

  [[nodiscard]] size_t componentCount() const override { return components.size(); }
};

// ── XML builders ──────────────────────────────────────────────────────────
inline std::string makeSimpleXml(const std::string &id, const std::string &name,
                                 const std::string &summary = "",
                                 const std::string &type = "desktop-application") {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<components>\n"
         "  <component type=\"" +
         type +
         "\">\n"
         "    <id>" +
         id +
         "</id>\n"
         "    <name>" +
         name + "</name>\n" + (summary.empty() ? "" : "    <summary>" + summary + "</summary>\n") +
         "  </component>\n"
         "</components>\n";
}

inline Component makeComponent(std::string id, std::string name, std::string summary = "") {
  Component c;
  c.id = std::move(id);
  c.name = std::move(name);
  c.summary = std::move(summary);
  c.type = Component::Type::DESKTOP_APPLICATION;
  return c;
}
