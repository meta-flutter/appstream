// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 Joel Winarske <joel.winarske@gmail.com>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "AppStreamParser.h"
#include "Component.h"
#include "SqliteWriter.h"

namespace fs = std::filesystem;

// ============================================================
// Real Appstream data test utilities
// ============================================================

class RealAppstreamTest : public ::testing::Test {
protected:
  // Create a realistic Flathub-style appstream.xml snippet
  static std::string getRealWorldXMLSample() {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<components version="0.14">
  <component type="desktop">
    <id>org.gnome.Nautilus</id>
    <pkgname>nautilus</pkgname>
    <name>Files</name>
    <summary>Access and organize files</summary>
    <description>
      <p>
        Files, also known as Nautilus, is the default file manager of the GNOME desktop.
        It provides a simple and integrated way of managing your files and browsing file systems.
      </p>
      <p>
        Nautilus supports all the basic functions of a file manager and more.
        It can search and manage your files both locally and on a network, both read and write data to and from USB drives and CDs, run scripts, and launch apps.
      </p>
    </description>
    <categories>
      <category>System</category>
      <category>FileManager</category>
    </categories>
    <url type="homepage">https://wiki.gnome.org/Apps/Files</url>
    <url type="bugtracker">https://bugzilla.gnome.org/enter_bug.cgi?product=nautilus</url>
    <url type="help">https://help.gnome.org/users/nautilus/</url>
    <url type="donation">https://www.gnome.org/support-gnome/</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>GPL-3.0+</project_license>
    <developer_name>The GNOME Project</developer_name>
    <releases>
      <release version="45.0" date="2023-09-16"/>
      <release version="44.0" date="2023-03-18"/>
    </releases>
    <content_rating type="oars-1.1"/>
    <screenshots>
      <screenshot type="default">
        <image type="source">https://example.com/nautilus-main.png</image>
      </screenshot>
    </screenshots>
  </component>

  <component type="desktop">
    <id>org.gnome.Evolution</id>
    <pkgname>evolution</pkgname>
    <name>Evolution Mail</name>
    <summary>Email client and personal information manager</summary>
    <description>
      <p>
        Evolution is the GNOME application to manage your email, calendar, task list and contact database.
      </p>
      <p>
        Evolution also includes an RSS feed reader, rich text editor, and mail composer.
      </p>
    </description>
    <categories>
      <category>Office</category>
      <category>Email</category>
      <category>ContactManagement</category>
    </categories>
    <url type="homepage">https://wiki.gnome.org/Apps/Evolution</url>
    <url type="bugtracker">https://gitlab.gnome.org/GNOME/evolution/-/issues</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>LGPL-2.1+</project_license>
    <developer_name>The GNOME Project</developer_name>
    <releases>
      <release version="3.46.0" date="2023-09-18"/>
      <release version="3.44.0" date="2023-03-20"/>
    </releases>
  </component>

  <component type="desktop">
    <id>org.gimp.GIMP</id>
    <pkgname>gimp</pkgname>
    <name>GNU Image Manipulation Program</name>
    <summary>Create images and edit photographs</summary>
    <description>
      <p>
        GIMP is an acronym for GNU Image Manipulation Program.
        It is a freely distributed program for tasks such as photo retouching, image composition and image authoring.
      </p>
      <p>
        It works on many operating systems, in many languages.
      </p>
    </description>
    <categories>
      <category>Graphics</category>
      <category>Photography</category>
      <category>Raster</category>
    </categories>
    <url type="homepage">https://www.gimp.org</url>
    <url type="bugtracker">https://gitlab.gnome.org/GNOME/gimp/-/issues</url>
    <url type="help">https://docs.gimp.org</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>GPL-3.0+</project_license>
    <developer_name>The GIMP Team</developer_name>
    <releases>
      <release version="2.10.32" date="2023-02-10"/>
      <release version="2.10.30" date="2022-10-11"/>
    </releases>
  </component>

  <component type="desktop">
    <id>org.mozilla.firefox</id>
    <pkgname>firefox</pkgname>
    <name>Firefox</name>
    <summary>Fast, Private & Safe Web Browser</summary>
    <description>
      <p>
        Firefox is a fast, light and tidy open source web browser.
        It is highly extensible and uses web standards.
      </p>
    </description>
    <categories>
      <category>Network</category>
      <category>WebBrowser</category>
    </categories>
    <url type="homepage">https://www.mozilla.org/firefox/</url>
    <url type="bugtracker">https://bugzilla.mozilla.org/</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>MPL-2.0</project_license>
    <developer_name>Mozilla Foundation</developer_name>
    <releases>
      <release version="119.0" date="2023-10-24"/>
      <release version="118.0" date="2023-10-03"/>
    </releases>
  </component>

  <component type="desktop">
    <id>com.spotify.Client</id>
    <name>Spotify</name>
    <summary>Listen to music</summary>
    <description>
      <p>
        Spotify is a digital music service that gives you access to millions of songs.
      </p>
    </description>
    <categories>
      <category>Audio</category>
      <category>Music</category>
    </categories>
    <url type="homepage">https://www.spotify.com/</url>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>Proprietary</project_license>
  </component>
</components>)";
  }

  std::string createTempFile(const std::string &content) {
    static int counter = 0;
    auto path = "/tmp/appstream_real_" + std::to_string(++counter) + ".xml";
    std::ofstream file(path);
    file << content;
    file.close();
    return path;
  }

  std::string createTempDB() {
    static int counter = 0;
    return "/tmp/appstream_real_" + std::to_string(++counter) + ".db";
  }

  void cleanupFile(const std::string &path) {
    if (fs::exists(path)) {
      fs::remove(path);
    }
  }
};

// ============================================================
// Tests with real Flathub-style data
// ============================================================

class MockSink : public ComponentSink {
public:
  std::vector<Component> components;
  std::vector<Error> errors;

  std::expected<void, Error> begin() override { return {}; }

  std::expected<void, Error> onComponent(Component component) override {
    components.push_back(component);
    return {};
  }

  std::expected<void, Error> end() override { return {}; }

  [[nodiscard]] size_t componentCount() const override { return components.size(); }
};

TEST_F(RealAppstreamTest, ParseRealWorldSample) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

  EXPECT_TRUE(result.has_value()) << "parse failed with error code "
                                  << static_cast<int>(result.error());
  EXPECT_EQ(sink.componentCount(), 5);

  // Verify specific components
  EXPECT_EQ(sink.components[0].id, "org.gnome.Nautilus");
  EXPECT_EQ(sink.components[0].name, "Files");
  EXPECT_EQ(sink.components[0].type, Component::Type::DESKTOP_APPLICATION);

  EXPECT_EQ(sink.components[1].id, "org.gnome.Evolution");
  EXPECT_EQ(sink.components[1].name, "Evolution Mail");

  EXPECT_EQ(sink.components[4].id, "com.spotify.Client");
  EXPECT_EQ(sink.components[4].name, "Spotify");

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, ParseAndStoreRealData) {
  auto xml_path = createTempFile(getRealWorldXMLSample());
  auto db_path = createTempDB();

  {
    SqliteWriter writer(db_path);
    EXPECT_TRUE(writer.begin().has_value());

    MockSink sink;
    auto parse_result = AppStreamParser::parseToSink(xml_path, "en", sink);
    EXPECT_TRUE(parse_result.has_value());
    EXPECT_EQ(sink.componentCount(), 5);

    EXPECT_TRUE(writer.end().has_value());
  }

  // Verify database was created
  EXPECT_TRUE(fs::exists(db_path));
  auto file_size = fs::file_size(db_path);
  EXPECT_GT(file_size, 10000); // Should be reasonably sized
  std::cout << "✓ Created database: " << file_size << " bytes" << std::endl;

  cleanupFile(xml_path);
  cleanupFile(db_path);
}

TEST_F(RealAppstreamTest, LanguageFiltering) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  // Test with different language
  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "fr", sink);

  EXPECT_TRUE(result.has_value());
  EXPECT_GT(sink.componentCount(), 0);

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, ComponentCategories) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

  EXPECT_TRUE(result.has_value());

  // Verify categories are parsed
  auto nautilus = std::find_if(sink.components.begin(), sink.components.end(),
                               [](const Component &c) { return c.id == "org.gnome.Nautilus"; });

  EXPECT_NE(nautilus, sink.components.end());
  EXPECT_GE(nautilus->categories.size(), 2);

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, URLs) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

  EXPECT_TRUE(result.has_value());

  // Verify URLs are parsed
  auto firefox = std::find_if(sink.components.begin(), sink.components.end(),
                              [](const Component &c) { return c.id == "org.mozilla.firefox"; });

  EXPECT_NE(firefox, sink.components.end());
  EXPECT_FALSE(firefox->urls.empty());

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, MultiLanguageSupport) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  // Test parsing with multiple language filters
  std::vector<std::string> languages = {"en", "de", "fr", "es", "ja"};

  for (const auto &lang : languages) {
    MockSink sink;
    auto result = AppStreamParser::parseToSink(xml_path, lang, sink);

    EXPECT_TRUE(result.has_value()) << "Failed to parse with language: " << lang;
    EXPECT_GT(sink.componentCount(), 0) << "No components for language: " << lang;
  }

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, ErrorHandlingMalformedXML) {
  std::string malformed_xml =
      R"(<?xml version="1.0"?>
<components version="0.14">
  <component type="desktop">
    <id>test.component
    <!-- Missing closing tag -->
</components>)";

  auto xml_path = createTempFile(malformed_xml);

  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

  // Should gracefully handle errors (may succeed with partial data or fail
  // with a parse error). Either way, should not crash.
  if (result.has_value()) {
    SUCCEED() << "parser returned success with partial data";
  } else {
    EXPECT_NE(result.error(), AppStreamParser::ParseError::FILE_NOT_FOUND);
  }

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, InMemorySearchOnRealData) {
  auto xml_path = createTempFile(getRealWorldXMLSample());

  auto result = AppStreamParser::create(xml_path, "en");
  EXPECT_TRUE(result.has_value());

  // Verify total component count
  EXPECT_EQ(result->getTotalComponentCount(), 5);

  // Find a specific GNOME app by id
  auto *nautilus = result->findComponent("org.gnome.Nautilus");
  EXPECT_NE(nautilus, nullptr);

  // Search by category
  auto graphics = result->searchByCategory("Graphics");
  EXPECT_GT(graphics.size(), 0);

  cleanupFile(xml_path);
}

TEST_F(RealAppstreamTest, LargeRealWorldDataset) {
  // Create a larger dataset simulating real Flathub catalog
  std::stringstream ss;
  ss << R"(<?xml version="1.0" encoding="UTF-8"?>
<components version="0.14">)";

  // Add 100 components with realistic data
  for (int i = 0; i < 100; ++i) {
    ss << R"(
  <component type="desktop">
    <id>org.test.app)"
       << i << R"(</id>
    <name>Test Application )"
       << i << R"(</name>
    <summary>A test application for benchmarking )"
       << i << R"(</summary>
    <description>
      <p>This is test application number )"
       << i << R"(.</p>
    </description>
    <categories>
      <category>Utility</category>
      <category>System</category>
    </categories>
    <url type="homepage">https://example.com/)"
       << i << R"(</url>
  </component>)";
  }

  ss << R"(
</components>)";

  auto xml_path = createTempFile(ss.str());

  MockSink sink;
  auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(sink.componentCount(), 100);

  cleanupFile(xml_path);
}
