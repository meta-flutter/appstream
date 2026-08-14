// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
// Integration tests for AppStreamParser — both streaming (parseToSink) and
// in-memory (create) modes.
#include <limits>
#include <gtest/gtest.h>
#include <algorithm>
#include <set>

#include "AppStreamParser.h"
#include "test_helpers.h"

using PE = AppStreamParser::ParseError;

// ── Shared XML fixtures ───────────────────────────────────────────────────

static const std::string kOne = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.One</id>
    <name>Example One</name>
    <summary>First example</summary>
    <categories>
      <category>Utility</category>
      <category>Science</category>
    </categories>
    <keywords>
      <keyword>test</keyword>
      <keyword>example</keyword>
    </keywords>
    <url type="homepage">https://example.com</url>
    <url type="bugtracker">https://bugs.example.com</url>
  </component>
</components>
)xml";

static const std::string kThree = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.a.Alpha</id><name>Alpha</name><summary>First</summary>
    <categories><category>Network</category></categories>
    <keywords><keyword>alpha</keyword></keywords>
  </component>
  <component type="font">
    <id>com.b.Beta</id><name>Beta</name><summary>Second</summary>
    <categories><category>Science</category></categories>
    <keywords><keyword>beta</keyword></keywords>
  </component>
  <component type="addon">
    <id>com.c.Gamma</id><name>Gamma</name><summary>Third</summary>
    <categories><category>Science</category></categories>
    <keywords><keyword>gamma</keyword></keywords>
  </component>
</components>
)xml";

// ── parseToSink — error cases ─────────────────────────────────────────────

TEST(AppStreamParserSink, MissingFileReturnsMmapFailed) {
    VectorSink sink;
    auto r = AppStreamParser::parseToSink("/no/such/path/file.xml", "", sink);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PE::MMAP_FAILED);
}

TEST(AppStreamParserSink, MalformedXmlReturnsXmlParseError) {
    const TempFile f("<components><component type=unquoted><id>x</id></component></components>");
    VectorSink sink;
    auto r = AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PE::XML_PARSE_ERROR);
}

TEST(AppStreamParserSink, SinkErrorIsPropagated) {
    const TempFile f(kOne);
    VectorSink sink;
    sink.failOnComponent = true;
    auto r = AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PE::SINK_ERROR);
}

// ── parseToSink — happy paths ─────────────────────────────────────────────

TEST(AppStreamParserSink, EmptyRootProducesZeroComponents) {
    const TempFile f("<?xml version=\"1.0\"?><components/>");
    VectorSink sink;
    ASSERT_TRUE(AppStreamParser::parseToSink(f.str(), "", sink).has_value());
    EXPECT_EQ(sink.components.size(), 0u);
}

TEST(AppStreamParserSink, SingleComponentDelivered) {
    TempFile f(kOne);
    VectorSink sink;
    ASSERT_TRUE(AppStreamParser::parseToSink(f.str(), "", sink).has_value());
    ASSERT_EQ(sink.components.size(), 1u);
}

TEST(AppStreamParserSink, ComponentFieldsPreserved) {
    TempFile f(kOne);
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    const auto& c = sink.components[0];
    EXPECT_EQ(c.id,      "com.example.One");
    EXPECT_EQ(c.name,    "Example One");
    EXPECT_EQ(c.summary, "First example");
    EXPECT_EQ(c.type,    Component::Type::DESKTOP_APPLICATION);
}

TEST(AppStreamParserSink, CategoriesParsed) {
    TempFile f(kOne);
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    const auto& cats = sink.components[0].categories;
    ASSERT_EQ(cats.size(), 2u);
    EXPECT_TRUE(std::ranges::contains(cats, "Utility"));
    EXPECT_TRUE(std::ranges::contains(cats, "Science"));
}

TEST(AppStreamParserSink, KeywordsParsed) {
    TempFile f(kOne);
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    const auto& kws = sink.components[0].keywords;
    ASSERT_EQ(kws.size(), 2u);
    EXPECT_TRUE(std::ranges::contains(kws, "test"));
    EXPECT_TRUE(std::ranges::contains(kws, "example"));
}

TEST(AppStreamParserSink, UrlsParsed) {
    TempFile f(kOne);
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    const auto& c = sink.components[0];
    EXPECT_EQ(c.getUrl(Component::UrlType::HOMEPAGE),   "https://example.com");
    EXPECT_EQ(c.getUrl(Component::UrlType::BUGTRACKER), "https://bugs.example.com");
}

TEST(AppStreamParserSink, MultipleComponents) {
    TempFile f(kThree);
    VectorSink sink;
    ASSERT_TRUE(AppStreamParser::parseToSink(f.str(), "", sink).has_value());
    EXPECT_EQ(sink.components.size(), 3u);
}

TEST(AppStreamParserSink, MultipleComponentTypes) {
    TempFile f(kThree);
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    std::set<Component::Type> types;
    for (const auto& c : sink.components) types.insert(c.type);
    EXPECT_TRUE(types.contains(Component::Type::DESKTOP_APPLICATION));
    EXPECT_TRUE(types.contains(Component::Type::FONT));
    EXPECT_TRUE(types.contains(Component::Type::ADDON));
}

TEST(AppStreamParserSink, ReleaseParsed) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Rel</id>
    <name>Rel App</name>
    <releases>
      <release version="2.1.0" date="2024-06-01" type="stable" urgency="high"/>
    </releases>
  </component>
</components>)xml");
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_EQ(sink.components[0].releases.size(), 1u);
    const auto& rel = sink.components[0].releases[0];
    EXPECT_EQ(rel.version, "2.1.0");
    EXPECT_EQ(rel.date,    "2024-06-01");
    EXPECT_EQ(rel.type,    Component::ReleaseType::STABLE);
    EXPECT_EQ(rel.urgency, Component::ReleaseUrgency::HIGH);
}

TEST(AppStreamParserSink, DescriptionHtmlAccumulated) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Desc</id>
    <name>Desc App</name>
    <description><p>Hello <em>world</em></p></description>
  </component>
</components>)xml");
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    EXPECT_FALSE(sink.components[0].description.empty());
    EXPECT_NE(sink.components[0].description.find("<p>"), std::string::npos);
}

// ── language filter ───────────────────────────────────────────────────────

TEST(AppStreamParserSink, LangFilter_EnSkipsDeNameTag) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Lang</id>
    <name>Default Name</name>
    <name xml:lang="de">German Name</name>
    <summary>Test</summary>
  </component>
</components>)xml");
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "en", sink);
    ASSERT_EQ(sink.components.size(), 1u);
    EXPECT_EQ(sink.components[0].name, "Default Name");
}

TEST(AppStreamParserSink, LangFilter_EmptyKeepsDefaultOnly) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Lang</id>
    <name>Default Name</name>
    <name xml:lang="de">German Name</name>
    <summary>Test</summary>
  </component>
</components>)xml");
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_EQ(sink.components.size(), 1u);
    EXPECT_EQ(sink.components[0].name, "Default Name");  // "" = default only
}

TEST(AppStreamParserSink, LangFilter_DeMatchesDeTag) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Lang</id>
    <name>Default</name>
    <name xml:lang="de">German Name</name>
    <summary xml:lang="de">Deutsche Zusammenfassung</summary>
    <summary>English summary</summary>
  </component>
</components>)xml");
    VectorSink sink;
    AppStreamParser::parseToSink(f.str(), "de", sink);
    ASSERT_EQ(sink.components.size(), 1u);
    EXPECT_EQ(sink.components[0].name, "German Name");
}

// ── create() ─────────────────────────────────────────────────────────────

TEST(AppStreamParserCreate, MissingFileReturnsError) {
    auto r = AppStreamParser::create("/does/not/exist.xml", "");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), PE::MMAP_FAILED);
}

TEST(AppStreamParserCreate, SingleComponent) {
    TempFile f(kOne);
    auto parser = AppStreamParser::create(f.str(), "");
    ASSERT_TRUE(parser.has_value());
    EXPECT_EQ(parser->getTotalComponentCount(), 1u);
}

TEST(AppStreamParserCreate, FindComponentById) {
    TempFile f(kOne);
    auto parser = AppStreamParser::create(f.str(), "").value();
    const auto* c = parser.findComponent("com.example.One");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->name, "Example One");
}

TEST(AppStreamParserCreate, FindMissingComponentReturnsNull) {
    TempFile f(kOne);
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_EQ(parser.findComponent("com.does.not.Exist"), nullptr);
}

TEST(AppStreamParserCreate, MultipleComponents) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_EQ(parser.getTotalComponentCount(), 3u);
}

TEST(AppStreamParserCreate, GetUniqueCategories) {
    TempFile f(kThree);   // Network, Science (×2)
    auto parser = AppStreamParser::create(f.str(), "").value();
    auto cats = parser.getUniqueCategories();
    const std::set<std::string> catSet(cats.begin(), cats.end());
    EXPECT_EQ(catSet.size(), 2u);
    EXPECT_TRUE(catSet.contains("Network"));
    EXPECT_TRUE(catSet.contains("Science"));
}

TEST(AppStreamParserCreate, GetUniqueKeywords) {
    TempFile f(kThree);   // alpha, beta, gamma — all distinct
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_EQ(parser.getUniqueKeywords().size(), 3u);
}

TEST(AppStreamParserCreate, SearchByCategory_Hit) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    auto results = parser.searchByCategory("Science");
    EXPECT_EQ(results.size(), 2u);   // Beta and Gamma
}

TEST(AppStreamParserCreate, SearchByCategory_Miss) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_TRUE(parser.searchByCategory("NoSuchCategory").empty());
}

TEST(AppStreamParserCreate, SearchByKeyword_Hit) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    auto results = parser.searchByKeyword("alpha");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->id, "com.a.Alpha");
}

TEST(AppStreamParserCreate, SearchByKeyword_Miss) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_TRUE(parser.searchByKeyword("no-such-keyword").empty());
}

TEST(AppStreamParserCreate, GetSortedComponentsById_IsOrdered) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    auto sorted = parser.getSortedComponents(AppStreamParser::SortOption::BY_ID);
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_LE(sorted[0]->id, sorted[1]->id);
    EXPECT_LE(sorted[1]->id, sorted[2]->id);
}

TEST(AppStreamParserCreate, GetSortedComponentsByName_IsOrdered) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    auto sorted = parser.getSortedComponents(AppStreamParser::SortOption::BY_NAME);
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_LE(sorted[0]->name, sorted[1]->name);
    EXPECT_LE(sorted[1]->name, sorted[2]->name);
}

TEST(AppStreamParserCreate, DuplicateIdKeepsFirst) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.dup.App</id><name>Original</name>
  </component>
  <component type="desktop-application">
    <id>com.dup.App</id><name>Duplicate</name>
  </component>
</components>)xml");
    auto parser = AppStreamParser::create(f.str(), "").value();
    EXPECT_EQ(parser.getTotalComponentCount(), 1u);
    EXPECT_EQ(parser.findComponent("com.dup.App")->name, "Original");
}

TEST(AppStreamParserCreate, GetComponentsRangeYieldsAllComponents) {
    TempFile f(kThree);
    auto parser = AppStreamParser::create(f.str(), "").value();
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& c : parser.getComponents()) ++count;
    EXPECT_EQ(count, 3u);
}

TEST(AppStreamParserCreate, MoveConstruction) {
    TempFile f(kOne);
    auto p1 = AppStreamParser::create(f.str(), "").value();
    auto p2 = std::move(p1);
    EXPECT_EQ(p2.getTotalComponentCount(), 1u);
}

TEST(AppStreamParserCreate, MoveAssignment) {
    TempFile f1(kOne);
    TempFile f2(kThree);
    auto p1 = AppStreamParser::create(f1.str(), "").value();
    auto p2 = AppStreamParser::create(f2.str(), "").value();
    p1 = std::move(p2);
    EXPECT_EQ(p1.getTotalComponentCount(), 3u);
}


// ── Numeric overflow on untrusted attribute values ─────────────────────────
//
// Attribute values come from a catalog downloaded over the network. The digit
// accumulators used to compute `result * 10 + digit` without a bound, so a
// long enough run of digits overflowed. Signed overflow is undefined
// behavior, and UBSan fired on both of the inputs below. Accumulation now
// saturates, so these must parse cleanly and clamp.

TEST(AppStreamParserOverflow, OversizedTimestampDoesNotOverflow) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Overflow</id>
    <name>Overflow</name>
    <releases>
      <release version="1" timestamp="99999999999999999999999999"/>
    </releases>
  </component>
</components>)xml");
    VectorSink sink;
    const auto r = AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(sink.components.size(), 1u);
    ASSERT_EQ(sink.components[0].releases.size(), 1u);
    // A saturated epoch is not representable as a time_t, so gmtime_r fails
    // and the timestamp is reported as absent rather than as 1900-01-01.
    const auto& ts = sink.components[0].releases[0].timestamp;
    EXPECT_TRUE(ts.empty() || ts.find("1900-01-01") == std::string::npos);
}

TEST(AppStreamParserOverflow, OversizedDimensionsClampInsteadOfWrapping) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Overflow</id>
    <name>Overflow</name>
    <icon type="cached" width="99999999999999" height="7">a.png</icon>
  </component>
</components>)xml");
    VectorSink sink;
    const auto r = AppStreamParser::parseToSink(f.str(), "", sink);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(sink.components.size(), 1u);
    ASSERT_FALSE(sink.components[0].icons.empty());
    const auto& icon = sink.components[0].icons[0];
    EXPECT_EQ(icon.width, std::numeric_limits<int>::max());
    EXPECT_EQ(icon.height, 7);
}

TEST(AppStreamParserOverflow, NormalNumericValuesStillParse) {
    TempFile f(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component type="desktop-application">
    <id>com.example.Normal</id>
    <name>Normal</name>
    <icon type="cached" width="128" height="64">a.png</icon>
    <releases>
      <release version="1" timestamp="1700000000"/>
    </releases>
  </component>
</components>)xml");
    VectorSink sink;
    ASSERT_TRUE(AppStreamParser::parseToSink(f.str(), "", sink).has_value());
    ASSERT_FALSE(sink.components[0].icons.empty());
    EXPECT_EQ(sink.components[0].icons[0].width, 128);
    EXPECT_EQ(sink.components[0].icons[0].height, 64);
    ASSERT_EQ(sink.components[0].releases.size(), 1u);
    // 1700000000 == 2023-11-14T22:13:20Z
    EXPECT_EQ(sink.components[0].releases[0].timestamp, "2023-11-14T22:13:20Z");
}
