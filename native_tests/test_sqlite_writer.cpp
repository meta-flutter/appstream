// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
// Unit / integration tests for SqliteWriter.
// Each test writes to a unique temp DB path.
#include <filesystem>
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <vector>

#include "SqliteWriter.h"
#include "test_helpers.h"

// ── sqlite3 query helpers ─────────────────────────────────────────────────

/// Run a scalar query and return the first column of the first row as int64.
/// Returns -1 on any error.
static int64_t queryInt(const std::string &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    return -1;
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  int64_t result = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    result = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

/// Return the text value of column 0, row 0.
static std::string queryText(const std::string &path, const std::string &sql) {
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    return "";
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  std::string result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    if (t)
      result = t;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

static bool tableExists(const std::string &path, const std::string &table) {
  return queryInt(path, "SELECT COUNT(*) FROM sqlite_master "
                        "WHERE type='table' AND name='" +
                            table + "'") > 0;
}

// ── lifecycle ─────────────────────────────────────────────────────────────

TEST(SqliteWriter, BeginCreatesStagingFile) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  EXPECT_TRUE(std::filesystem::exists(db.str() + ".staging"));
}

TEST(SqliteWriter, EndAfterBeginCreatesDbFile) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_TRUE(db.exists());
}

TEST(SqliteWriter, EndAfterBeginRemovesStagingFile) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_FALSE(std::filesystem::exists(db.str() + ".staging"));
}

TEST(SqliteWriter, EmptyWriteProducesZeroComponentCount) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(w.componentCount(), 0u);
}

// ── single component ──────────────────────────────────────────────────────

TEST(SqliteWriter, SingleComponentIncreasesCount) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.onComponent(makeComponent("com.test.App", "Test")).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(w.componentCount(), 1u);
}

TEST(SqliteWriter, SingleComponentAppearsInDb) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.onComponent(makeComponent("com.db.Check", "DB Check", "Great")).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM components"), 1);
}

TEST(SqliteWriter, ComponentFieldsStoredCorrectly) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());

  Component c = makeComponent("com.fields.App", "Fields App", "Nice summary");
  c.pkgname = "fields-app";
  c.projectLicense = "Apache-2.0";
  c.developer.name = "Test Developer";
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());

  EXPECT_EQ(queryText(db.str(), "SELECT name FROM components WHERE id='com.fields.App'"),
            "Fields App");
  EXPECT_EQ(queryText(db.str(), "SELECT summary FROM components WHERE id='com.fields.App'"),
            "Nice summary");
  EXPECT_EQ(queryText(db.str(), "SELECT pkgname FROM components WHERE id='com.fields.App'"),
            "fields-app");
  EXPECT_EQ(queryText(db.str(), "SELECT project_license FROM components WHERE id='com.fields.App'"),
            "Apache-2.0");
  EXPECT_EQ(queryText(db.str(), "SELECT developer_name FROM components WHERE id='com.fields.App'"),
            "Test Developer");
}

// ── multiple components ───────────────────────────────────────────────────

TEST(SqliteWriter, FiveComponentsAllWritten) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(w.onComponent(
                     makeComponent("com.multi.App" + std::to_string(i), "App " + std::to_string(i)))
                    .has_value());
  }
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(w.componentCount(), 5u);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM components"), 5);
}

// ── batch commit ──────────────────────────────────────────────────────────

TEST(SqliteWriter, BatchSizeOneCommitsAfterEachComponent) {
  TempPath db;
  SqliteWriter w(db.str(), 1);
  ASSERT_TRUE(w.begin().has_value());
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(
        w.onComponent(makeComponent("com.batch.App" + std::to_string(i), "Batch")).has_value());
  }
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(w.componentCount(), 4u);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM components"), 4);
}

TEST(SqliteWriter, BatchSizeLargerThanComponentCount) {
  TempPath db;
  SqliteWriter w(db.str(), 1000);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.onComponent(makeComponent("com.single.App", "Single")).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(w.componentCount(), 1u);
}

// ── categories & keywords ─────────────────────────────────────────────────

TEST(SqliteWriter, CategoriesStoredInJunctionTable) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.cat.App", "Cat");
  c.categories = {"Utility", "Science"};
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM categories"), 2);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_categories"), 2);
}

TEST(SqliteWriter, KeywordsStoredInJunctionTable) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.kw.App", "KW");
  c.keywords = {"alpha", "beta", "gamma"};
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM keywords"), 3);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_keywords"), 3);
}

TEST(SqliteWriter, SharedCategoryInternedOnce) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c1 = makeComponent("com.a.App", "A");
  c1.categories = {"Shared"};
  Component c2 = makeComponent("com.b.App", "B");
  c2.categories = {"Shared"};
  ASSERT_TRUE(w.onComponent(std::move(c1)).has_value());
  ASSERT_TRUE(w.onComponent(std::move(c2)).has_value());
  ASSERT_TRUE(w.end().has_value());
  // One unique category row, two junction rows.
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM categories"), 1);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_categories"), 2);
}

// ── releases ─────────────────────────────────────────────────────────────

TEST(SqliteWriter, ReleaseStoredCorrectly) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.rel.App", "Rel");
  Component::Release rel;
  rel.version = "3.0.0";
  rel.date = "2025-01-01";
  rel.type = Component::ReleaseType::STABLE;
  rel.urgency = Component::ReleaseUrgency::MEDIUM;
  c.releases.push_back(std::move(rel));
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM releases"), 1);
  EXPECT_EQ(queryText(db.str(), "SELECT version FROM releases"), "3.0.0");
}

// ── icons & URLs ──────────────────────────────────────────────────────────

TEST(SqliteWriter, IconsStoredInIconTable) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.icon.App", "Icon");
  Component::Icon icon;
  icon.type = Component::IconType::CACHED;
  icon.value = "com.icon.App.png";
  icon.width = 64;
  icon.height = 64;
  c.icons.push_back(std::move(icon));
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_icons"), 1);
  EXPECT_EQ(queryText(db.str(), "SELECT value FROM component_icons"), "com.icon.App.png");
}

TEST(SqliteWriter, UrlsStoredInUrlTable) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.url.App", "URL");
  c.setUrl(Component::UrlType::HOMEPAGE, "https://home.com");
  c.setUrl(Component::UrlType::BUGTRACKER, "https://bugs.com");
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_urls"), 2);
}

// ── screenshots ───────────────────────────────────────────────────────────

TEST(SqliteWriter, ScreenshotAndImagesStored) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.ss.App", "SS");
  Component::Screenshot ss;
  ss.isDefault = true;
  ss.caption = "Main window";
  Component::Image img;
  img.url = "https://example.com/screenshot.png";
  img.type = "source";
  img.width = 1280;
  img.height = 720;
  ss.images.push_back(std::move(img));
  c.screenshots.push_back(std::move(ss));
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM screenshots"), 1);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM screenshot_images"), 1);
  EXPECT_EQ(queryInt(db.str(), "SELECT is_default FROM screenshots"), 1);
}

// ── schema completeness ───────────────────────────────────────────────────

TEST(SqliteWriter, AllRequiredTablesPresent) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(w.end().has_value());

  const char *required[] = {
      "components",
      "component_urls",
      "component_icons",
      "categories",
      "component_categories",
      "keywords",
      "component_keywords",
      "releases",
      "release_issues",
      "release_artifacts",
      "artifact_checksums",
      "artifact_sizes",
      "component_languages",
      "component_compulsory",
      "screenshots",
      "screenshot_images",
      "screenshot_videos",
      "content_rating_attrs",
      "provides_binaries",
      "provides_libraries",
      "provides_mediatypes",
      "provides_dbus",
      "provides_ids",
      "branding_colors",
      "component_extends",
      "component_suggests",
      "component_relations",
      "component_custom",
      "component_translations",
      "write_progress",
      "components_fts",
  };
  for (const char *t : required) {
    EXPECT_TRUE(tableExists(db.str(), t)) << "Missing table: " << t;
  }
}

// ── FTS ───────────────────────────────────────────────────────────────────

TEST(SqliteWriter, FtsTablePopulatedAfterEnd) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  ASSERT_TRUE(
      w.onComponent(makeComponent("com.fts.App", "FTS Search", "Findable content")).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM components_fts"), 1);
}

// ── provides / branding / relations ──────────────────────────────────────

TEST(SqliteWriter, ProvidesStoredCorrectly) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.prov.App", "Prov");
  c.provides.binaries = {"my-binary"};
  c.provides.libraries = {"libfoo.so"};
  c.provides.mediatypes = {"image/png"};
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM provides_binaries"), 1);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM provides_libraries"), 1);
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM provides_mediatypes"), 1);
}

TEST(SqliteWriter, BrandingColorsStored) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.brand.App", "Brand");
  c.branding_colors.push_back({"light", "#ffffff"});
  c.branding_colors.push_back({"dark", "#000000"});
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM branding_colors"), 2);
}

TEST(SqliteWriter, RelationsStoredCorrectly) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.rel2.App", "Rel2");
  Component::Relation req;
  req.type = "memory";
  req.value = "2000";
  req.compare = "ge";
  c.requires_.push_back(req);
  Component::Relation rec;
  rec.type = "display_length";
  rec.value = "small";
  c.recommends.push_back(rec);
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_relations"), 2);
}

TEST(SqliteWriter, CustomKeyValueStored) {
  TempPath db;
  SqliteWriter w(db.str(), 200);
  ASSERT_TRUE(w.begin().has_value());
  Component c = makeComponent("com.custom.App", "Custom");
  c.custom.push_back({"GnomeSoftware::packagename", "custom-app"});
  ASSERT_TRUE(w.onComponent(std::move(c)).has_value());
  ASSERT_TRUE(w.end().has_value());
  EXPECT_EQ(queryInt(db.str(), "SELECT COUNT(*) FROM component_custom"), 1);
  EXPECT_EQ(queryText(db.str(), "SELECT value FROM component_custom"), "custom-app");
}
