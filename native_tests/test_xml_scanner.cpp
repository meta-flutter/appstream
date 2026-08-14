// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
// Unit tests for XmlScanner
#include "XmlScanner.h"
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using ET = XmlScanner::EventType;
using Err = XmlScanner::Error;

// ── Helpers ───────────────────────────────────────────────────────────────

/// Collect all events up to END_OF_DOCUMENT. Fails the test on any error.
static std::vector<XmlScanner::Event> scanAll(const std::string &xml) {
  XmlScanner s(xml.data(), xml.size());
  std::vector<XmlScanner::Event> events;
  while (!s.atEnd()) {
    auto r = s.next();
    EXPECT_TRUE(r.has_value()) << "Unexpected scan error";
    if (!r)
      break;
    if (r->type == ET::END_OF_DOCUMENT)
      break;
    events.push_back(*r);
  }
  return events;
}

// ── atEnd / empty buffer ──────────────────────────────────────────────────

TEST(XmlScanner, EmptyBufferIsAtEnd) {
  XmlScanner s("", 0);
  EXPECT_TRUE(s.atEnd());
}

TEST(XmlScanner, WhitespaceOnlyYieldsEndOfDocument) {
  XmlScanner s("   \n\t  ", 7);
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, ET::END_OF_DOCUMENT);
}

// ── start / end tags ──────────────────────────────────────────────────────

TEST(XmlScanner, SimpleStartAndEndTag) {
  std::string xml = "<root></root>";
  auto ev = scanAll(xml);
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_EQ(ev[0].type, ET::START_ELEMENT);
  EXPECT_EQ(ev[0].name, "root");
  EXPECT_EQ(ev[1].type, ET::END_ELEMENT);
  EXPECT_EQ(ev[1].name, "root");
}

TEST(XmlScanner, SelfClosingTagProducesStartThenEnd) {
  // Self-closing tags schedule the END_ELEMENT; must call next() twice
  auto xml = "<br/>";
  XmlScanner s(xml, std::strlen(xml));
  auto r1 = s.next();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->type, ET::START_ELEMENT);
  auto r2 = s.next();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(r2->type, ET::END_ELEMENT);
  EXPECT_EQ(r1->name, r2->name);
}

TEST(XmlScanner, SelfClosingTagWithSpaceBeforeSlash) {
  auto xml = "<br />";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, ET::START_ELEMENT);
  EXPECT_EQ(r->name, "br");
}

TEST(XmlScanner, NestedElements) {
  auto xml = "<a><b><c/></b></a>";
  XmlScanner s(xml, std::strlen(xml));
  std::vector<XmlScanner::Event> ev;
  while (!s.atEnd()) {
    auto r = s.next();
    if (!r || r->type == ET::END_OF_DOCUMENT)
      break;
    ev.push_back(*r);
  }
  ASSERT_EQ(ev.size(), 6u);
  EXPECT_EQ(ev[0].name, "a");
  EXPECT_EQ(ev[0].type, ET::START_ELEMENT);
  EXPECT_EQ(ev[1].name, "b");
  EXPECT_EQ(ev[1].type, ET::START_ELEMENT);
  EXPECT_EQ(ev[2].name, "c");
  EXPECT_EQ(ev[2].type, ET::START_ELEMENT);
  EXPECT_EQ(ev[3].name, "c");
  EXPECT_EQ(ev[3].type, ET::END_ELEMENT);
  EXPECT_EQ(ev[4].name, "b");
  EXPECT_EQ(ev[4].type, ET::END_ELEMENT);
  EXPECT_EQ(ev[5].name, "a");
  EXPECT_EQ(ev[5].type, ET::END_ELEMENT);
}

// ── attributes ────────────────────────────────────────────────────────────

TEST(XmlScanner, SingleDoubleQuotedAttribute) {
  auto xml = R"(<tag key="value"/>)";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->attributes.size(), 1u);
  EXPECT_EQ(r->attributes[0].name, "key");
  EXPECT_EQ(r->attributes[0].value, "value");
}

TEST(XmlScanner, SingleSingleQuotedAttribute) {
  const char *xml = "<tag key='value'/>";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->attributes.size(), 1u);
  EXPECT_EQ(r->attributes[0].value, "value");
}

TEST(XmlScanner, MultipleAttributes) {
  const char *xml = R"(<tag a="1" b="2" c="3"/>)";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->attributes.size(), 3u);
  EXPECT_EQ(r->attributes[0].name, "a");
  EXPECT_EQ(r->attributes[1].name, "b");
  EXPECT_EQ(r->attributes[2].name, "c");
  EXPECT_EQ(r->attributes[0].value, "1");
  EXPECT_EQ(r->attributes[2].value, "3");
}

TEST(XmlScanner, AttributeOnStartTag) {
  const char *xml = R"(<root type="desktop-application"></root>)";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, ET::START_ELEMENT);
  ASSERT_EQ(r->attributes.size(), 1u);
  EXPECT_EQ(r->attributes[0].name, "type");
}

// ── text content ──────────────────────────────────────────────────────────

TEST(XmlScanner, PlainTextContent) {
  auto xml = "<t>hello world</t>";
  XmlScanner s(xml, std::strlen(xml));
  std::vector<XmlScanner::Event> ev;
  while (!s.atEnd()) {
    auto r = s.next();
    if (!r || r->type == ET::END_OF_DOCUMENT)
      break;
    ev.push_back(*r);
  }
  ASSERT_EQ(ev.size(), 3u);
  EXPECT_EQ(ev[1].type, ET::TEXT);
  EXPECT_EQ(ev[1].text(), "hello world");
  EXPECT_FALSE(ev[1].text_has_entities);
}

TEST(XmlScanner, TextEventTextMethod_NoEntities) {
  std::string xml = "<t>plain</t>";
  auto ev = scanAll(xml);
  EXPECT_EQ(ev[1].text(), "plain");
  EXPECT_FALSE(ev[1].text_has_entities);
  // text_view should point into the source buffer (zero-copy)
  EXPECT_FALSE(ev[1].text_view.empty());
}

TEST(XmlScanner, TextEventTextMethod_WithEntities) {
  auto ev = scanAll("<t>&amp;foo</t>");
  EXPECT_TRUE(ev[1].text_has_entities);
  EXPECT_EQ(ev[1].text(), "&foo");
  // text_owned is populated; text_view is unused
  EXPECT_FALSE(ev[1].text_owned.empty());
}

// ── entity decoding ───────────────────────────────────────────────────────

TEST(XmlScanner, EntityAmp) {
  EXPECT_EQ(scanAll("<t>&amp;</t>")[1].text(), "&");
}
TEST(XmlScanner, EntityLt) {
  EXPECT_EQ(scanAll("<t>&lt;</t>")[1].text(), "<");
}
TEST(XmlScanner, EntityGt) {
  EXPECT_EQ(scanAll("<t>&gt;</t>")[1].text(), ">");
}
TEST(XmlScanner, EntityQuot) {
  EXPECT_EQ(scanAll("<t>&quot;</t>")[1].text(), "\"");
}
TEST(XmlScanner, EntityApos) {
  EXPECT_EQ(scanAll("<t>&apos;</t>")[1].text(), "'");
}

TEST(XmlScanner, NumericDecimalEntity) {
  EXPECT_EQ(scanAll("<t>&#65;</t>")[1].text(), "A");
}

TEST(XmlScanner, NumericHexEntityLower) {
  EXPECT_EQ(scanAll("<t>&#x41;</t>")[1].text(), "A");
}

TEST(XmlScanner, NumericHexEntityUpper) {
  EXPECT_EQ(scanAll("<t>&#X41;</t>")[1].text(), "A");
}

TEST(XmlScanner, MultipleEntitiesInOneText) {
  EXPECT_EQ(scanAll("<t>&lt;b&gt;</t>")[1].text(), "<b>");
}

TEST(XmlScanner, TextMixedPlainAndEntities) {
  EXPECT_EQ(scanAll("<t>a &amp; b</t>")[1].text(), "a & b");
}

// ── comment skipping ──────────────────────────────────────────────────────

TEST(XmlScanner, CommentSkippedBetweenElements) {
  std::string xml = "<root><!-- skip --><child/></root>";
  auto ev = scanAll(xml);
  // root-start, child-start, child-end, root-end (comment absent)
  ASSERT_EQ(ev.size(), 4u);
  EXPECT_EQ(ev[1].name, "child");
}

TEST(XmlScanner, CommentSkippedAtDocStart) {
  auto xml = "<!-- header --><root/>";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  // Comment is skipped; next event is the START_ELEMENT for root
  EXPECT_EQ(r->name, "root");
  EXPECT_EQ(r->type, ET::START_ELEMENT);
}

TEST(XmlScanner, CdataSectionPreservesRawContent) {
  auto xml = "<t><![CDATA[raw & <stuff>]]></t>";
  XmlScanner s(xml, std::strlen(xml));
  std::vector<XmlScanner::Event> ev;
  while (!s.atEnd()) {
    auto r = s.next();
    if (!r || r->type == ET::END_OF_DOCUMENT)
      break;
    ev.push_back(*r);
  }
  ASSERT_EQ(ev.size(), 3u);
  EXPECT_EQ(ev[1].type, ET::TEXT);
  EXPECT_EQ(ev[1].text(), "raw & <stuff>");
}

TEST(XmlScanner, CdataWithBracketContent) {
  const char *xml = "<t><![CDATA[a]b]]></t>";
  XmlScanner s(xml, std::strlen(xml));
  std::vector<XmlScanner::Event> ev;
  while (!s.atEnd()) {
    auto r = s.next();
    if (!r || r->type == ET::END_OF_DOCUMENT)
      break;
    ev.push_back(*r);
  }
  EXPECT_EQ(ev[1].text(), "a]b");
}

TEST(XmlScanner, XmlDeclarationSkipped) {
  auto xml = R"(<?xml version="1.0" encoding="UTF-8"?><root/>)";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  // XML declaration is skipped; next event is the START_ELEMENT for root
  EXPECT_EQ(r->name, "root");
  EXPECT_EQ(r->type, ET::START_ELEMENT);
}

TEST(XmlScanner, BomSkipped) {
  // UTF-8 BOM: EF BB BF
  std::string xml = "\xEF\xBB\xBF<root/>";
  XmlScanner s(xml.data(), xml.size());
  auto r = s.next();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, ET::START_ELEMENT);
  EXPECT_EQ(r->name, "root");
}

// ── error cases ───────────────────────────────────────────────────────────

TEST(XmlScanner, MalformedTagUnquotedAttributeReturnsMalformedTag) {
  XmlScanner s("<tag attr=unquoted>", 19);
  auto r = s.next();
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), Err::MALFORMED_TAG);
}

TEST(XmlScanner, UnexpectedEofInCdataReturnsUnexpectedEof) {
  auto xml = "<t><![CDATA[unterminated";
  XmlScanner s(xml, std::strlen(xml));
  s.next(); // START_ELEMENT <t>
  auto r = s.next();
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), Err::UNEXPECTED_EOF);
}

TEST(XmlScanner, UnexpectedEofMidTag) {
  // Attribute with '=' but no value at all (EOF follows)
  auto xml = "<tag a=";
  XmlScanner s(xml, std::strlen(xml));
  auto r = s.next();
  EXPECT_FALSE(r.has_value());
  // Either UNEXPECTED_EOF or MALFORMED_TAG depending on exact position
  EXPECT_TRUE(r.error() == Err::UNEXPECTED_EOF || r.error() == Err::MALFORMED_TAG);
}

// ── atEnd after full document ─────────────────────────────────────────────

TEST(XmlScanner, AtEndAfterDocument) {
  XmlScanner s("<r/>", 4);
  s.next(); // START
  s.next(); // END
  // May still have whitespace / nothing left — next call gives END_OF_DOC
  // then atEnd().
  while (!s.atEnd()) {
    auto r = s.next();
    if (!r || r->type == ET::END_OF_DOCUMENT)
      break;
  }
  EXPECT_TRUE(s.atEnd());
}
