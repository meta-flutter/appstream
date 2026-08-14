// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
// Unit tests for StringPool
#include "StringPool.h"
#include <gtest/gtest.h>

// ── intern(string_view) ───────────────────────────────────────────────────

TEST(StringPool, InternByViewReturnsSamePointerForSameInput) {
  StringPool pool;
  auto v1 = pool.intern(std::string_view{"hello"});
  auto v2 = pool.intern(std::string_view{"hello"});
  EXPECT_EQ(v1.data(), v2.data());
  EXPECT_EQ(v1, "hello");
}

TEST(StringPool, InternByStringReturnsSamePointerForSameInput) {
  StringPool pool;
  std::string s = "world";
  auto v1 = pool.intern(s);
  auto v2 = pool.intern(s);
  EXPECT_EQ(v1.data(), v2.data());
}

TEST(StringPool, InternByViewAndByStringReturnSamePointer) {
  StringPool pool;
  std::string s = "shared";
  auto v1 = pool.intern(std::string_view{s});
  auto v2 = pool.intern(s);
  EXPECT_EQ(v1.data(), v2.data());
  EXPECT_EQ(v1, v2);
}

TEST(StringPool, DifferentStringsHaveDifferentPointers) {
  StringPool pool;
  auto v1 = pool.intern(std::string_view{"foo"});
  auto v2 = pool.intern(std::string_view{"bar"});
  EXPECT_NE(v1, v2);
  EXPECT_NE(v1.data(), v2.data());
}

// ── size() ────────────────────────────────────────────────────────────────

TEST(StringPool, SizeStartsAtZero) {
  StringPool pool;
  EXPECT_EQ(pool.size(), 0u);
}

TEST(StringPool, SizeIncrementsOnNewStrings) {
  StringPool pool;
  EXPECT_EQ(pool.size(), 0u);
  [[maybe_unused]] auto _a = pool.intern(std::string_view{"a"});
  EXPECT_EQ(pool.size(), 1u);
  [[maybe_unused]] auto _b = pool.intern(std::string_view{"b"});
  EXPECT_EQ(pool.size(), 2u);
}

TEST(StringPool, SizeDoesNotChangeOnDuplicate) {
  StringPool pool;
  [[maybe_unused]] auto _x1 = pool.intern(std::string_view{"x"});
  [[maybe_unused]] auto _x2 = pool.intern(std::string_view{"x"});
  EXPECT_EQ(pool.size(), 1u);
}

TEST(StringPool, SizeCountsBothViewAndStringInserts) {
  StringPool pool;
  [[maybe_unused]] auto _ = pool.intern(std::string_view{"one"});
  [[maybe_unused]] auto _2 = pool.intern(std::string{"two"});
  EXPECT_EQ(pool.size(), 2u);
}

// ── edge cases ────────────────────────────────────────────────────────────

TEST(StringPool, EmptyStringIsInterned) {
  StringPool pool;
  auto v1 = pool.intern(std::string_view{""});
  auto v2 = pool.intern(std::string_view{""});
  EXPECT_TRUE(v1.empty());
  EXPECT_EQ(v1.data(), v2.data());
  EXPECT_EQ(pool.size(), 1u);
}

TEST(StringPool, ViewRemainsValidAfterOriginalModified) {
  StringPool pool;
  std::string original = "stable";
  auto view = pool.intern(original);
  original = "changed";      // mutate the source
  EXPECT_EQ(view, "stable"); // pool copy unaffected
}

TEST(StringPool, LongStringInterned) {
  StringPool pool;
  std::string long_str(10'000, 'Z');
  auto v = pool.intern(long_str);
  EXPECT_EQ(v.size(), 10'000u);
  EXPECT_EQ(pool.intern(long_str).data(), v.data());
}

TEST(StringPool, StressTestManyDistinctStrings) {
  StringPool pool;
  constexpr int N = 2000;
  std::vector<std::string_view> views;
  views.reserve(N);
  for (int i = 0; i < N; ++i) {
    views.push_back(pool.intern("str_" + std::to_string(i)));
  }
  EXPECT_EQ(pool.size(), static_cast<size_t>(N));
  // Every re-intern must return the exact same pointer.
  for (int i = 0; i < N; ++i) {
    EXPECT_EQ(pool.intern("str_" + std::to_string(i)).data(), views[static_cast<size_t>(i)].data());
  }
}
