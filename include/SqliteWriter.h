// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

#ifndef SQLITEWRITER_H
#define SQLITEWRITER_H

#include "ComponentSink.h"
#include <cstdint>
#include <string>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

/// Streams components to the SQLite via the staging file and atomic swap.
/// See prior documentation for transaction strategy and crash safety.
class SqliteWriter final : public ComponentSink {
public:
  explicit SqliteWriter(std::string dbPath, size_t batchSize = 200);
  ~SqliteWriter() noexcept override;

  SqliteWriter(const SqliteWriter &) = delete;
  SqliteWriter &operator=(const SqliteWriter &) = delete;

  std::expected<void, Error> begin() override;
  std::expected<void, Error> onComponent(Component component) override;
  std::expected<void, Error> end() override;
  [[nodiscard]] size_t componentCount() const override { return count_; }
  [[nodiscard]] const std::string &stagingPath() const { return stagingPath_; }

private:
  std::string dbPath_;
  std::string stagingPath_;
  size_t batchSize_;
  sqlite3 *db_ = nullptr;
  size_t count_ = 0;
  size_t batchCount_ = 0;
  bool inTransaction_ = false;
  bool endCalled_ = false;

  // Interning caches
  std::unordered_map<std::string, int64_t> categoryIds_;
  std::unordered_map<std::string, int64_t> keywordIds_;
  int64_t nextCategoryId_ = 1;
  int64_t nextKeywordId_ = 1;
  int64_t nextReleaseId_ = 1;
  int64_t nextArtifactId_ = 1;
  int64_t nextScreenshotId_ = 1;

  // Prepared statements
  sqlite3_stmt *stmtInsertComponent_ = nullptr;
  sqlite3_stmt *stmtInsertUrl_ = nullptr;
  sqlite3_stmt *stmtInsertIcon_ = nullptr;
  sqlite3_stmt *stmtInsertCategory_ = nullptr;
  sqlite3_stmt *stmtInsertComponentCategory_ = nullptr;
  sqlite3_stmt *stmtInsertKeyword_ = nullptr;
  sqlite3_stmt *stmtInsertComponentKeyword_ = nullptr;
  sqlite3_stmt *stmtInsertRelease_ = nullptr;
  sqlite3_stmt *stmtInsertIssue_ = nullptr;
  sqlite3_stmt *stmtInsertArtifact_ = nullptr;
  sqlite3_stmt *stmtInsertArtifactChecksum_ = nullptr;
  sqlite3_stmt *stmtInsertArtifactSize_ = nullptr;
  sqlite3_stmt *stmtInsertLanguage_ = nullptr;
  sqlite3_stmt *stmtInsertCompulsory_ = nullptr;
  sqlite3_stmt *stmtInsertScreenshot_ = nullptr;
  sqlite3_stmt *stmtInsertScreenshotImage_ = nullptr;
  sqlite3_stmt *stmtInsertScreenshotVideo_ = nullptr;
  sqlite3_stmt *stmtInsertContentAttr_ = nullptr;
  sqlite3_stmt *stmtInsertProvidesBinary_ = nullptr;
  sqlite3_stmt *stmtInsertProvidesLibrary_ = nullptr;
  sqlite3_stmt *stmtInsertProvidesMediatype_ = nullptr;
  sqlite3_stmt *stmtInsertProvidesDbus_ = nullptr;
  sqlite3_stmt *stmtInsertProvidesId_ = nullptr;
  sqlite3_stmt *stmtInsertBrandingColor_ = nullptr;
  sqlite3_stmt *stmtInsertExtends_ = nullptr;
  sqlite3_stmt *stmtInsertSuggestsId_ = nullptr;
  sqlite3_stmt *stmtInsertRelation_ = nullptr;
  sqlite3_stmt *stmtInsertCustom_ = nullptr;
  sqlite3_stmt *stmtInsertTranslation_ = nullptr;
  sqlite3_stmt *stmtInsertFieldTranslation_ = nullptr;
  sqlite3_stmt *stmtUpdateProgress_ = nullptr;

  bool createSchema();
  bool prepareStatements();
  void finalizeStatements();
  bool createIndices();
  bool buildFts();
  bool beginTransaction();
  bool commitBatch();
  bool setProgress(const char *key, const std::string &value);
  int64_t internCategory(const std::string &name);
  int64_t internKeyword(const std::string &name);
  void closeDb();
  void removeStagingFiles();
  bool atomicSwap();

  void bindText(sqlite3_stmt *s, int i, const std::string &t);
  void bindTextOrNull(sqlite3_stmt *s, int i, const std::string &t);
  void bindInt(sqlite3_stmt *s, int i, int64_t v);
  void bindIntOrNull(sqlite3_stmt *s, int i, std::optional<int> v);
  bool stepAndReset(sqlite3_stmt *s);
  bool exec(const char *sql);
};

#endif // SQLITEWRITER_H