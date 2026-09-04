// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

#include "SqliteWriter.h"
#include "../include/spdlog.h"
#include <cstdint>
#include <cstdio>
#include <sqlite3.h>
#include <string>

// ============================================================
// Lifecycle / helpers (unchanged from prior version)
// ============================================================

SqliteWriter::SqliteWriter(std::string dbPath, size_t batchSize)
    : dbPath_(std::move(dbPath)), stagingPath_(dbPath_ + ".staging"), batchSize_(batchSize) {
}

SqliteWriter::~SqliteWriter() noexcept {
  try {
    if (db_) {
      if (inTransaction_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
      }
      closeDb();
      if (!endCalled_) {
        spdlog::warn("SqliteWriter destroyed without end() — staging left at: {}", stagingPath_);
      }
    }
  } catch (...) {
    // Swallow: destructors must not throw. spdlog formatting is the only
    // realistic source of exceptions here, and we don't want to std::terminate
    // the parsing process if logging fails during teardown.
  }
}

void SqliteWriter::closeDb() {
  finalizeStatements();
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void SqliteWriter::removeStagingFiles() {
  (void)std::remove(stagingPath_.c_str());
  (void)std::remove((stagingPath_ + "-wal").c_str());
  (void)std::remove((stagingPath_ + "-shm").c_str());
}

bool SqliteWriter::atomicSwap() {
  (void)std::remove((dbPath_ + "-wal").c_str());
  (void)std::remove((dbPath_ + "-shm").c_str());
  if (std::rename(stagingPath_.c_str(), dbPath_.c_str()) != 0) {
    spdlog::error("rename failed: {} → {}", stagingPath_, dbPath_);
    return false;
  }
  (void)std::remove((stagingPath_ + "-wal").c_str());
  (void)std::remove((stagingPath_ + "-shm").c_str());
  return true;
}

void SqliteWriter::bindText(sqlite3_stmt *s, int i, const std::string &t) {
  sqlite3_bind_text(s, i, t.data(), static_cast<int>(t.size()), SQLITE_TRANSIENT);
}
void SqliteWriter::bindTextOrNull(sqlite3_stmt *s, int i, const std::string &t) {
  t.empty() ? sqlite3_bind_null(s, i)
            : sqlite3_bind_text(s, i, t.data(), static_cast<int>(t.size()), SQLITE_TRANSIENT);
}
void SqliteWriter::bindInt(sqlite3_stmt *s, int i, int64_t v) {
  sqlite3_bind_int64(s, i, v);
}
void SqliteWriter::bindIntOrNull(sqlite3_stmt *s, int i, std::optional<int> v) {
  v ? sqlite3_bind_int(s, i, *v) : sqlite3_bind_null(s, i);
}
bool SqliteWriter::stepAndReset(sqlite3_stmt *s) {
  int rc = sqlite3_step(s);
  sqlite3_reset(s);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    spdlog::error("step: {}", sqlite3_errmsg(db_));
    return false;
  }
  return true;
}
bool SqliteWriter::exec(const char *sql) {
  char *e = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &e);
  if (rc != SQLITE_OK) {
    spdlog::error("exec: {}", e ? e : "?");
    sqlite3_free(e);
    return false;
  }
  return true;
}

// ============================================================
// Schema
// ============================================================

bool SqliteWriter::createSchema() {
  return exec(R"SQL(
CREATE TABLE IF NOT EXISTS write_progress (
    key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS components (
    id TEXT PRIMARY KEY NOT NULL,
    component_type INTEGER NOT NULL DEFAULT 0,
    priority INTEGER NOT NULL DEFAULT 0,
    merge TEXT,
    name TEXT NOT NULL,
    name_variant_suffix TEXT,
    summary TEXT,
    description TEXT,
    pkgname TEXT,
    source_pkgname TEXT,
    project_license TEXT,
    metadata_license TEXT,
    project_group TEXT,
    media_baseurl TEXT,
    architecture TEXT,
    bundle_type INTEGER,
    bundle_id TEXT,
    developer_id TEXT,
    developer_name TEXT,
    launchable_type INTEGER,
    launchable_value TEXT,
    content_rating_type TEXT,
    agreement TEXT
);

CREATE TABLE IF NOT EXISTS component_urls (
    component_id TEXT NOT NULL, url_type INTEGER NOT NULL, url TEXT NOT NULL,
    PRIMARY KEY (component_id, url_type)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS component_icons (
    id INTEGER PRIMARY KEY, component_id TEXT NOT NULL,
    icon_type INTEGER NOT NULL, value TEXT NOT NULL,
    width INTEGER, height INTEGER, scale INTEGER
);

CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE IF NOT EXISTS component_categories (
    component_id TEXT NOT NULL, category_id INTEGER NOT NULL,
    PRIMARY KEY (component_id, category_id)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS keywords (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE IF NOT EXISTS component_keywords (
    component_id TEXT NOT NULL, keyword_id INTEGER NOT NULL,
    PRIMARY KEY (component_id, keyword_id)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS releases (
    id INTEGER PRIMARY KEY, component_id TEXT NOT NULL,
    release_type INTEGER, version TEXT, date TEXT, timestamp TEXT,
    date_eol TEXT, urgency INTEGER, description TEXT, url TEXT
);

CREATE TABLE IF NOT EXISTS release_issues (
    id INTEGER PRIMARY KEY, release_id INTEGER NOT NULL,
    issue_type INTEGER, url TEXT, value TEXT
);

CREATE TABLE IF NOT EXISTS release_artifacts (
    id INTEGER PRIMARY KEY, release_id INTEGER NOT NULL, location TEXT
);

CREATE TABLE IF NOT EXISTS artifact_checksums (
    artifact_id INTEGER NOT NULL, checksum_type TEXT NOT NULL, value TEXT NOT NULL,
    PRIMARY KEY (artifact_id, checksum_type)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS artifact_sizes (
    artifact_id INTEGER NOT NULL, size_type TEXT NOT NULL, value INTEGER NOT NULL,
    PRIMARY KEY (artifact_id, size_type)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS component_languages (
    component_id TEXT NOT NULL, language TEXT NOT NULL,
    PRIMARY KEY (component_id, language)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS component_compulsory (
    component_id TEXT NOT NULL, desktop INTEGER NOT NULL,
    PRIMARY KEY (component_id, desktop)
) WITHOUT ROWID;

-- Screenshots
CREATE TABLE IF NOT EXISTS screenshots (
    id INTEGER PRIMARY KEY, component_id TEXT NOT NULL,
    is_default INTEGER NOT NULL DEFAULT 0, caption TEXT
);

CREATE TABLE IF NOT EXISTS screenshot_images (
    id INTEGER PRIMARY KEY, screenshot_id INTEGER NOT NULL,
    url TEXT NOT NULL, type TEXT, width INTEGER, height INTEGER
);

CREATE TABLE IF NOT EXISTS screenshot_videos (
    id INTEGER PRIMARY KEY, screenshot_id INTEGER NOT NULL,
    url TEXT NOT NULL, codec TEXT, container TEXT, width INTEGER, height INTEGER
);

-- Content rating attributes
CREATE TABLE IF NOT EXISTS content_rating_attrs (
    component_id TEXT NOT NULL, attr_id TEXT NOT NULL, value TEXT NOT NULL,
    PRIMARY KEY (component_id, attr_id)
) WITHOUT ROWID;

-- Provides
CREATE TABLE IF NOT EXISTS provides_binaries (
    component_id TEXT NOT NULL, name TEXT NOT NULL,
    PRIMARY KEY (component_id, name)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS provides_libraries (
    component_id TEXT NOT NULL, name TEXT NOT NULL,
    PRIMARY KEY (component_id, name)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS provides_mediatypes (
    component_id TEXT NOT NULL, mediatype TEXT NOT NULL,
    PRIMARY KEY (component_id, mediatype)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS provides_dbus (
    component_id TEXT NOT NULL, bus_type TEXT NOT NULL, name TEXT NOT NULL,
    PRIMARY KEY (component_id, bus_type, name)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS provides_ids (
    component_id TEXT NOT NULL, provided_id TEXT NOT NULL,
    PRIMARY KEY (component_id, provided_id)
) WITHOUT ROWID;

-- Branding
CREATE TABLE IF NOT EXISTS branding_colors (
    component_id TEXT NOT NULL, scheme_preference TEXT NOT NULL, color TEXT NOT NULL,
    PRIMARY KEY (component_id, scheme_preference)
) WITHOUT ROWID;

-- Cross-references
CREATE TABLE IF NOT EXISTS component_extends (
    component_id TEXT NOT NULL, extends_id TEXT NOT NULL,
    PRIMARY KEY (component_id, extends_id)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS component_suggests (
    component_id TEXT NOT NULL, suggested_id TEXT NOT NULL,
    PRIMARY KEY (component_id, suggested_id)
) WITHOUT ROWID;

-- Relations (requires / recommends)
CREATE TABLE IF NOT EXISTS component_relations (
    id INTEGER PRIMARY KEY, component_id TEXT NOT NULL,
    relation_kind TEXT NOT NULL, -- 'requires' or 'recommends'
    relation_type TEXT NOT NULL, -- 'display_length', 'control', etc.
    value TEXT, compare TEXT, version TEXT
);

-- Custom key-value
CREATE TABLE IF NOT EXISTS component_custom (
    component_id TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,
    PRIMARY KEY (component_id, key)
) WITHOUT ROWID;

-- Translation info
CREATE TABLE IF NOT EXISTS component_translations (
    component_id TEXT NOT NULL, type TEXT NOT NULL, value TEXT,
    PRIMARY KEY (component_id, type)
) WITHOUT ROWID;

-- Localized field translations (xml:lang variants)
CREATE TABLE IF NOT EXISTS component_field_translations (
    component_id TEXT NOT NULL,
    field TEXT NOT NULL,
    language TEXT NOT NULL,
    value TEXT NOT NULL,
    PRIMARY KEY (component_id, field, language)
) WITHOUT ROWID;
)SQL");
}

// ============================================================
// Prepared statements
// ============================================================

bool SqliteWriter::prepareStatements() {
  auto p = [this](const char *sql, sqlite3_stmt *&s) -> bool {
    int rc = sqlite3_prepare_v2(db_, sql, -1, &s, nullptr);
    if (rc != SQLITE_OK) {
      spdlog::error("prepare: {}", sqlite3_errmsg(db_));
      return false;
    }
    return true;
  };
  bool ok = true;

  ok = ok && p("INSERT OR IGNORE INTO components "
               "(id,component_type,priority,merge,name,name_variant_suffix,"
               "summary,description,pkgname,source_pkgname,project_license,"
               "metadata_license,project_group,"
               "media_baseurl,architecture,bundle_type,bundle_id,developer_id,"
               "developer_name,"
               "launchable_type,launchable_value,content_rating_type,agreement) "
               "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
               stmtInsertComponent_);

  ok = ok && p("INSERT OR IGNORE INTO component_urls VALUES (?,?,?)", stmtInsertUrl_);
  ok = ok && p("INSERT INTO component_icons "
               "(component_id,icon_type,value,width,height,scale) VALUES "
               "(?,?,?,?,?,?)",
               stmtInsertIcon_);
  ok = ok && p("INSERT OR IGNORE INTO categories VALUES (?,?)", stmtInsertCategory_);
  ok = ok &&
       p("INSERT OR IGNORE INTO component_categories VALUES (?,?)", stmtInsertComponentCategory_);
  ok = ok && p("INSERT OR IGNORE INTO keywords VALUES (?,?)", stmtInsertKeyword_);
  ok =
      ok && p("INSERT OR IGNORE INTO component_keywords VALUES (?,?)", stmtInsertComponentKeyword_);
  ok = ok && p("INSERT INTO releases "
               "(id,component_id,release_type,version,date,timestamp,date_eol,"
               "urgency,description,url) VALUES (?,?,?,?,?,?,?,?,?,?)",
               stmtInsertRelease_);
  ok = ok && p("INSERT INTO release_issues (release_id,issue_type,url,value) "
               "VALUES (?,?,?,?)",
               stmtInsertIssue_);
  ok = ok && p("INSERT INTO release_artifacts (id,release_id,location) VALUES (?,?,?)",
               stmtInsertArtifact_);
  ok = ok &&
       p("INSERT OR IGNORE INTO artifact_checksums VALUES (?,?,?)", stmtInsertArtifactChecksum_);
  ok = ok && p("INSERT OR IGNORE INTO artifact_sizes VALUES (?,?,?)", stmtInsertArtifactSize_);
  ok = ok && p("INSERT OR IGNORE INTO component_languages VALUES (?,?)", stmtInsertLanguage_);
  ok = ok && p("INSERT OR IGNORE INTO component_compulsory VALUES (?,?)", stmtInsertCompulsory_);

  ok = ok && p("INSERT INTO screenshots (id,component_id,is_default,caption) "
               "VALUES (?,?,?,?)",
               stmtInsertScreenshot_);
  ok = ok && p("INSERT INTO screenshot_images "
               "(screenshot_id,url,type,width,height) VALUES (?,?,?,?,?)",
               stmtInsertScreenshotImage_);
  ok = ok && p("INSERT INTO screenshot_videos "
               "(screenshot_id,url,codec,container,width,height) VALUES (?,?,?,?,?,?)",
               stmtInsertScreenshotVideo_);

  ok = ok && p("INSERT OR IGNORE INTO content_rating_attrs VALUES (?,?,?)", stmtInsertContentAttr_);
  ok = ok && p("INSERT OR IGNORE INTO provides_binaries VALUES (?,?)", stmtInsertProvidesBinary_);
  ok = ok && p("INSERT OR IGNORE INTO provides_libraries VALUES (?,?)", stmtInsertProvidesLibrary_);
  ok = ok &&
       p("INSERT OR IGNORE INTO provides_mediatypes VALUES (?,?)", stmtInsertProvidesMediatype_);
  ok = ok && p("INSERT OR IGNORE INTO provides_dbus VALUES (?,?,?)", stmtInsertProvidesDbus_);
  ok = ok && p("INSERT OR IGNORE INTO provides_ids VALUES (?,?)", stmtInsertProvidesId_);
  ok = ok && p("INSERT OR IGNORE INTO branding_colors VALUES (?,?,?)", stmtInsertBrandingColor_);
  ok = ok && p("INSERT OR IGNORE INTO component_extends VALUES (?,?)", stmtInsertExtends_);
  ok = ok && p("INSERT OR IGNORE INTO component_suggests VALUES (?,?)", stmtInsertSuggestsId_);
  ok = ok && p("INSERT INTO component_relations "
               "(component_id,relation_kind,relation_type,value,compare,"
               "version) VALUES (?,?,?,?,?,?)",
               stmtInsertRelation_);
  ok = ok && p("INSERT OR IGNORE INTO component_custom VALUES (?,?,?)", stmtInsertCustom_);
  ok = ok &&
       p("INSERT OR IGNORE INTO component_translations VALUES (?,?,?)", stmtInsertTranslation_);
  ok = ok && p("INSERT OR IGNORE INTO component_field_translations VALUES (?,?,?,?)",
               stmtInsertFieldTranslation_);
  ok = ok && p("INSERT OR REPLACE INTO write_progress VALUES (?,?)", stmtUpdateProgress_);

  return ok;
}

void SqliteWriter::finalizeStatements() {
  auto f = [](sqlite3_stmt *&s) {
    if (s) {
      sqlite3_finalize(s);
      s = nullptr;
    }
  };
  f(stmtInsertComponent_);
  f(stmtInsertUrl_);
  f(stmtInsertIcon_);
  f(stmtInsertCategory_);
  f(stmtInsertComponentCategory_);
  f(stmtInsertKeyword_);
  f(stmtInsertComponentKeyword_);
  f(stmtInsertRelease_);
  f(stmtInsertIssue_);
  f(stmtInsertArtifact_);
  f(stmtInsertArtifactChecksum_);
  f(stmtInsertArtifactSize_);
  f(stmtInsertLanguage_);
  f(stmtInsertCompulsory_);
  f(stmtInsertScreenshot_);
  f(stmtInsertScreenshotImage_);
  f(stmtInsertScreenshotVideo_);
  f(stmtInsertContentAttr_);
  f(stmtInsertProvidesBinary_);
  f(stmtInsertProvidesLibrary_);
  f(stmtInsertProvidesMediatype_);
  f(stmtInsertProvidesDbus_);
  f(stmtInsertProvidesId_);
  f(stmtInsertBrandingColor_);
  f(stmtInsertExtends_);
  f(stmtInsertSuggestsId_);
  f(stmtInsertRelation_);
  f(stmtInsertCustom_);
  f(stmtInsertTranslation_);
  f(stmtInsertFieldTranslation_);
  f(stmtUpdateProgress_);
}

// ============================================================
// Interning
// ============================================================

int64_t SqliteWriter::internCategory(const std::string &name) {
  if (auto it = categoryIds_.find(name); it != categoryIds_.end())
    return it->second;
  int64_t id = nextCategoryId_++;
  bindInt(stmtInsertCategory_, 1, id);
  bindText(stmtInsertCategory_, 2, name);
  stepAndReset(stmtInsertCategory_);
  categoryIds_.emplace(name, id);
  return id;
}

int64_t SqliteWriter::internKeyword(const std::string &name) {
  if (auto it = keywordIds_.find(name); it != keywordIds_.end())
    return it->second;
  int64_t id = nextKeywordId_++;
  bindInt(stmtInsertKeyword_, 1, id);
  bindText(stmtInsertKeyword_, 2, name);
  stepAndReset(stmtInsertKeyword_);
  keywordIds_.emplace(name, id);
  return id;
}

// ============================================================
// Transaction management
// ============================================================

bool SqliteWriter::beginTransaction() {
  if (inTransaction_)
    return true;
  if (!exec("BEGIN IMMEDIATE"))
    return false;
  inTransaction_ = true;
  return true;
}

bool SqliteWriter::commitBatch() {
  if (!inTransaction_)
    return true;
  setProgress("components_written", std::to_string(count_));
  if (!exec("COMMIT"))
    return false;
  inTransaction_ = false;
  batchCount_ = 0;
  return true;
}

bool SqliteWriter::setProgress(const char *key, const std::string &value) {
  if (!stmtUpdateProgress_)
    return false;
  std::string k(key);
  bindText(stmtUpdateProgress_, 1, k);
  bindText(stmtUpdateProgress_, 2, value);
  return stepAndReset(stmtUpdateProgress_);
}

// ============================================================
// Indices & FTS
// ============================================================

bool SqliteWriter::createIndices() {
  return exec(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_comp_type ON components(component_type);
        CREATE INDEX IF NOT EXISTS idx_urls_comp ON component_urls(component_id);
        CREATE INDEX IF NOT EXISTS idx_icons_comp ON component_icons(component_id);
        CREATE INDEX IF NOT EXISTS idx_catjoin_comp ON component_categories(component_id);
        CREATE INDEX IF NOT EXISTS idx_kwjoin_comp ON component_keywords(component_id);
        CREATE INDEX IF NOT EXISTS idx_rel_comp ON releases(component_id);
        CREATE INDEX IF NOT EXISTS idx_ss_comp ON screenshots(component_id);
        CREATE INDEX IF NOT EXISTS idx_ss_img ON screenshot_images(screenshot_id);
        CREATE INDEX IF NOT EXISTS idx_ext_comp ON component_extends(component_id);
        CREATE INDEX IF NOT EXISTS idx_sug_comp ON component_suggests(component_id);
        CREATE INDEX IF NOT EXISTS idx_rel_kind ON component_relations(component_id);
    )SQL");
}

bool SqliteWriter::buildFts() {
  return exec(R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS components_fts USING fts5(
            name, summary, description, content='components', content_rowid='rowid');
        INSERT INTO components_fts(rowid, name, summary, description)
            SELECT rowid, name, COALESCE(summary,''), COALESCE(description,'') FROM components;
    )SQL");
}

// ============================================================
// begin()
// ============================================================

std::expected<void, ComponentSink::Error> SqliteWriter::begin() {
  removeStagingFiles();
  int rc = sqlite3_open(stagingPath_.c_str(), &db_);
  if (rc != SQLITE_OK)
    return std::unexpected(Error::DATABASE_ERROR);

  if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL") ||
      !exec("PRAGMA temp_store=MEMORY") || !exec("PRAGMA cache_size=-2000"))
    return std::unexpected(Error::DATABASE_ERROR);

  if (!createSchema() || !prepareStatements())
    return std::unexpected(Error::DATABASE_ERROR);

  if (!beginTransaction())
    return std::unexpected(Error::DATABASE_ERROR);
  setProgress("status", "writing");
  setProgress("components_written", "0");
  if (!commitBatch() || !beginTransaction())
    return std::unexpected(Error::DATABASE_ERROR);

  count_ = 0;
  batchCount_ = 0;
  endCalled_ = false;
  spdlog::info("SQLite writer ready (batch {}): {}", batchSize_, stagingPath_);
  return {};
}

// ============================================================
// onComponent()
// ============================================================

std::expected<void, ComponentSink::Error> SqliteWriter::onComponent(Component comp) {

  // 1. Component
  int col = 1;
  bindText(stmtInsertComponent_, col++, comp.id);
  bindInt(stmtInsertComponent_, col++, static_cast<int64_t>(comp.type));
  bindInt(stmtInsertComponent_, col++, comp.priority);
  bindTextOrNull(stmtInsertComponent_, col++, comp.merge);
  bindText(stmtInsertComponent_, col++, comp.name);
  bindTextOrNull(stmtInsertComponent_, col++, comp.name_variant_suffix);
  bindTextOrNull(stmtInsertComponent_, col++, comp.summary);
  bindTextOrNull(stmtInsertComponent_, col++, comp.description);
  bindTextOrNull(stmtInsertComponent_, col++, comp.pkgname);
  bindTextOrNull(stmtInsertComponent_, col++, comp.source_pkgname);
  bindTextOrNull(stmtInsertComponent_, col++, comp.projectLicense);
  bindTextOrNull(stmtInsertComponent_, col++, comp.metadata_license);
  bindTextOrNull(stmtInsertComponent_, col++, comp.project_group);
  bindTextOrNull(stmtInsertComponent_, col++, comp.media_baseurl);
  bindTextOrNull(stmtInsertComponent_, col++, comp.architecture);
  bindInt(stmtInsertComponent_, col++, static_cast<int64_t>(comp.bundle.type));
  bindTextOrNull(stmtInsertComponent_, col++, comp.bundle.id);
  bindTextOrNull(stmtInsertComponent_, col++, comp.developer.id);
  bindTextOrNull(stmtInsertComponent_, col++, comp.developer.name);
  bindInt(stmtInsertComponent_, col++, static_cast<int64_t>(comp.launchable.type));
  bindTextOrNull(stmtInsertComponent_, col++, comp.launchable.value);
  bindTextOrNull(stmtInsertComponent_, col++, comp.content_rating.type);
  bindTextOrNull(stmtInsertComponent_, col++, comp.agreement);
  if (!stepAndReset(stmtInsertComponent_))
    return std::unexpected(Error::DATABASE_ERROR);

// Helper macro for simple junction inserts
#define INSERT2(stmt, a, b)                                                                        \
  do {                                                                                             \
    bindText(stmt, 1, a);                                                                          \
    bindText(stmt, 2, b);                                                                          \
    if (!stepAndReset(stmt))                                                                       \
      return std::unexpected(Error::DATABASE_ERROR);                                               \
  } while (0)
#define INSERT2I(stmt, a, b)                                                                       \
  do {                                                                                             \
    bindText(stmt, 1, a);                                                                          \
    bindInt(stmt, 2, b);                                                                           \
    if (!stepAndReset(stmt))                                                                       \
      return std::unexpected(Error::DATABASE_ERROR);                                               \
  } while (0)

  // 2. URLs
  for (const auto &[t, u] : comp.urls) {
    bindText(stmtInsertUrl_, 1, comp.id);
    bindInt(stmtInsertUrl_, 2, static_cast<int64_t>(t));
    bindText(stmtInsertUrl_, 3, u);
    if (!stepAndReset(stmtInsertUrl_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 3. Icons
  for (const auto &ic : comp.icons) {
    bindText(stmtInsertIcon_, 1, comp.id);
    bindInt(stmtInsertIcon_, 2, static_cast<int64_t>(ic.type));
    bindText(stmtInsertIcon_, 3, ic.value);
    bindIntOrNull(stmtInsertIcon_, 4, ic.width);
    bindIntOrNull(stmtInsertIcon_, 5, ic.height);
    bindIntOrNull(stmtInsertIcon_, 6, ic.scale);
    if (!stepAndReset(stmtInsertIcon_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 4. Categories & Keywords
  for (const auto &c : comp.categories) {
    INSERT2I(stmtInsertComponentCategory_, comp.id, internCategory(c));
  }
  for (const auto &k : comp.keywords) {
    INSERT2I(stmtInsertComponentKeyword_, comp.id, internKeyword(k));
  }

  // 5. Releases
  for (const auto &rel : comp.releases) {
    int64_t rid = nextReleaseId_++;
    bindInt(stmtInsertRelease_, 1, rid);
    bindText(stmtInsertRelease_, 2, comp.id);
    bindInt(stmtInsertRelease_, 3, static_cast<int64_t>(rel.type));
    bindTextOrNull(stmtInsertRelease_, 4, rel.version);
    bindTextOrNull(stmtInsertRelease_, 5, rel.date);
    bindTextOrNull(stmtInsertRelease_, 6, rel.timestamp);
    bindTextOrNull(stmtInsertRelease_, 7, rel.date_eol);
    bindInt(stmtInsertRelease_, 8, static_cast<int64_t>(rel.urgency));
    bindTextOrNull(stmtInsertRelease_, 9, rel.description);
    bindTextOrNull(stmtInsertRelease_, 10, rel.url);
    if (!stepAndReset(stmtInsertRelease_))
      return std::unexpected(Error::DATABASE_ERROR);

    for (const auto &iss : rel.issues) {
      bindInt(stmtInsertIssue_, 1, rid);
      bindInt(stmtInsertIssue_, 2, static_cast<int64_t>(iss.type));
      bindTextOrNull(stmtInsertIssue_, 3, iss.url);
      bindTextOrNull(stmtInsertIssue_, 4, iss.value);
      if (!stepAndReset(stmtInsertIssue_))
        return std::unexpected(Error::DATABASE_ERROR);
    }

    for (const auto &art : rel.artifacts) {
      int64_t aid = nextArtifactId_++;
      bindInt(stmtInsertArtifact_, 1, aid);
      bindInt(stmtInsertArtifact_, 2, rid);
      bindTextOrNull(stmtInsertArtifact_, 3, art.location);
      if (!stepAndReset(stmtInsertArtifact_))
        return std::unexpected(Error::DATABASE_ERROR);
      for (const auto &[ct, cv] : art.checksums) {
        bindInt(stmtInsertArtifactChecksum_, 1, aid);
        bindText(stmtInsertArtifactChecksum_, 2, ct);
        bindText(stmtInsertArtifactChecksum_, 3, cv);
        if (!stepAndReset(stmtInsertArtifactChecksum_))
          return std::unexpected(Error::DATABASE_ERROR);
      }
      for (const auto &[st, sv] : art.sizes) {
        bindInt(stmtInsertArtifactSize_, 1, aid);
        bindText(stmtInsertArtifactSize_, 2, st);
        bindInt(stmtInsertArtifactSize_, 3, static_cast<int64_t>(sv));
        if (!stepAndReset(stmtInsertArtifactSize_))
          return std::unexpected(Error::DATABASE_ERROR);
      }
    }
  }

  // 6. Languages
  for (const auto &l : comp.supportedLanguages) {
    INSERT2(stmtInsertLanguage_, comp.id, l);
  }

  // 7. Compulsory
  for (auto d : comp.compulsory_for_desktop) {
    INSERT2I(stmtInsertCompulsory_, comp.id, static_cast<int64_t>(d));
  }

  // 8. Screenshots
  for (const auto &ss : comp.screenshots) {
    int64_t ssid = nextScreenshotId_++;
    bindInt(stmtInsertScreenshot_, 1, ssid);
    bindText(stmtInsertScreenshot_, 2, comp.id);
    bindInt(stmtInsertScreenshot_, 3, ss.isDefault ? 1 : 0);
    bindTextOrNull(stmtInsertScreenshot_, 4, ss.caption);
    if (!stepAndReset(stmtInsertScreenshot_))
      return std::unexpected(Error::DATABASE_ERROR);

    for (const auto &img : ss.images) {
      bindInt(stmtInsertScreenshotImage_, 1, ssid);
      bindText(stmtInsertScreenshotImage_, 2, img.url);
      bindTextOrNull(stmtInsertScreenshotImage_, 3, img.type);
      bindIntOrNull(stmtInsertScreenshotImage_, 4, img.width);
      bindIntOrNull(stmtInsertScreenshotImage_, 5, img.height);
      if (!stepAndReset(stmtInsertScreenshotImage_))
        return std::unexpected(Error::DATABASE_ERROR);
    }
    for (const auto &vid : ss.videos) {
      bindInt(stmtInsertScreenshotVideo_, 1, ssid);
      bindText(stmtInsertScreenshotVideo_, 2, vid.url);
      bindTextOrNull(stmtInsertScreenshotVideo_, 3, vid.codec);
      bindTextOrNull(stmtInsertScreenshotVideo_, 4, vid.container);
      bindIntOrNull(stmtInsertScreenshotVideo_, 5, vid.width);
      bindIntOrNull(stmtInsertScreenshotVideo_, 6, vid.height);
      if (!stepAndReset(stmtInsertScreenshotVideo_))
        return std::unexpected(Error::DATABASE_ERROR);
    }
  }

  // 9. Content rating attributes
  for (const auto &[aid, val] : comp.content_rating.attributes) {
    bindText(stmtInsertContentAttr_, 1, comp.id);
    bindText(stmtInsertContentAttr_, 2, aid);
    bindText(stmtInsertContentAttr_, 3, val);
    if (!stepAndReset(stmtInsertContentAttr_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 10. Provides
  for (const auto &b : comp.provides.binaries) {
    INSERT2(stmtInsertProvidesBinary_, comp.id, b);
  }
  for (const auto &l : comp.provides.libraries) {
    INSERT2(stmtInsertProvidesLibrary_, comp.id, l);
  }
  for (const auto &m : comp.provides.mediatypes) {
    INSERT2(stmtInsertProvidesMediatype_, comp.id, m);
  }
  for (const auto &[bt, bn] : comp.provides.dbus) {
    bindText(stmtInsertProvidesDbus_, 1, comp.id);
    bindText(stmtInsertProvidesDbus_, 2, bt);
    bindText(stmtInsertProvidesDbus_, 3, bn);
    if (!stepAndReset(stmtInsertProvidesDbus_))
      return std::unexpected(Error::DATABASE_ERROR);
  }
  for (const auto &pid : comp.provides.ids) {
    INSERT2(stmtInsertProvidesId_, comp.id, pid);
  }

  // 11. Branding
  for (const auto &[scheme, color] : comp.branding_colors) {
    bindText(stmtInsertBrandingColor_, 1, comp.id);
    bindText(stmtInsertBrandingColor_, 2, scheme);
    bindText(stmtInsertBrandingColor_, 3, color);
    if (!stepAndReset(stmtInsertBrandingColor_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 12. Extends, Suggests
  for (const auto &e : comp.extends) {
    INSERT2(stmtInsertExtends_, comp.id, e);
  }
  for (const auto &s : comp.suggests_ids) {
    INSERT2(stmtInsertSuggestsId_, comp.id, s);
  }

  // 13. Relations (requires + recommends)
  auto insertRelations = [&](const std::vector<Component::Relation> &rels, const char *kind) {
    std::string k(kind);
    for (const auto &r : rels) {
      bindText(stmtInsertRelation_, 1, comp.id);
      bindText(stmtInsertRelation_, 2, k);
      bindText(stmtInsertRelation_, 3, r.type);
      bindTextOrNull(stmtInsertRelation_, 4, r.value);
      bindTextOrNull(stmtInsertRelation_, 5, r.compare);
      bindTextOrNull(stmtInsertRelation_, 6, r.version);
      if (!stepAndReset(stmtInsertRelation_))
        return false;
    }
    return true;
  };
  if (!insertRelations(comp.requires_, "requires"))
    return std::unexpected(Error::DATABASE_ERROR);
  if (!insertRelations(comp.recommends, "recommends"))
    return std::unexpected(Error::DATABASE_ERROR);

  // 14. Custom
  for (const auto &[k, v] : comp.custom) {
    bindText(stmtInsertCustom_, 1, comp.id);
    bindText(stmtInsertCustom_, 2, k);
    bindText(stmtInsertCustom_, 3, v);
    if (!stepAndReset(stmtInsertCustom_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 15. Translations
  for (const auto &[t, v] : comp.translations) {
    bindText(stmtInsertTranslation_, 1, comp.id);
    bindText(stmtInsertTranslation_, 2, t);
    bindTextOrNull(stmtInsertTranslation_, 3, v);
    if (!stepAndReset(stmtInsertTranslation_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

  // 16. Field translations (xml:lang variants)
  for (const auto &ft : comp.field_translations) {
    bindText(stmtInsertFieldTranslation_, 1, comp.id);
    bindText(stmtInsertFieldTranslation_, 2, ft.field);
    bindText(stmtInsertFieldTranslation_, 3, ft.language);
    bindText(stmtInsertFieldTranslation_, 4, ft.value);
    if (!stepAndReset(stmtInsertFieldTranslation_))
      return std::unexpected(Error::DATABASE_ERROR);
  }

#undef INSERT2
#undef INSERT2I

  ++count_;
  ++batchCount_;
  if (batchCount_ >= batchSize_) {
    if (!commitBatch() || !beginTransaction())
      return std::unexpected(Error::DATABASE_ERROR);
  }
  return {};
}

// ============================================================
// end()
// ============================================================

std::expected<void, ComponentSink::Error> SqliteWriter::end() {
  if (inTransaction_) {
    if (!commitBatch())
      return std::unexpected(Error::DATABASE_ERROR);
  }

  if (!beginTransaction())
    return std::unexpected(Error::DATABASE_ERROR);
  setProgress("status", "indexing");
  if (!exec("COMMIT"))
    return std::unexpected(Error::DATABASE_ERROR);
  inTransaction_ = false;

  if (!createIndices())
    return std::unexpected(Error::DATABASE_ERROR);

  if (!beginTransaction())
    return std::unexpected(Error::DATABASE_ERROR);
  if (!buildFts())
    return std::unexpected(Error::DATABASE_ERROR);
  setProgress("status", "complete");
  if (!exec("COMMIT"))
    return std::unexpected(Error::DATABASE_ERROR);
  inTransaction_ = false;

  finalizeStatements();
  exec("PRAGMA optimize");
  exec("PRAGMA wal_checkpoint(TRUNCATE)");
  closeDb();

  if (!atomicSwap())
    return std::unexpected(Error::IO_ERROR);
  endCalled_ = true;
  spdlog::info("Database complete: {} components at {}", count_, dbPath_);
  return {};
}