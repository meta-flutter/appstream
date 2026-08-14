// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>
// Unit tests for Component — enum conversions, setUrl/getUrl,
// addSupportedLanguage, shrinkToFit, and default field values.
#include "Component.h"
#include <gtest/gtest.h>
#include <set>

// ── Component::Type ───────────────────────────────────────────────────────

TEST(ComponentType, DesktopApplication) {
  EXPECT_EQ(Component::stringToComponentType("desktop-application"),
            Component::Type::DESKTOP_APPLICATION);
}
TEST(ComponentType, LegacyDesktopAlias) {
  EXPECT_EQ(Component::stringToComponentType("desktop"), Component::Type::DESKTOP_APPLICATION);
}
TEST(ComponentType, AllKnownTypes) {
  using T = Component::Type;
  const std::pair<std::string_view, T> cases[] = {
      {"generic", T::GENERIC},
      {"console-application", T::CONSOLE_APPLICATION},
      {"web-application", T::WEB_APPLICATION},
      {"addon", T::ADDON},
      {"font", T::FONT},
      {"codec", T::CODEC},
      {"input-method", T::INPUT_METHOD},
      {"firmware", T::FIRMWARE},
      {"driver", T::DRIVER},
      {"localization", T::LOCALIZATION},
      {"service", T::SERVICE},
      {"repository", T::REPOSITORY},
      {"operating-system", T::OPERATING_SYSTEM},
      {"icon-theme", T::ICON_THEME},
      {"runtime", T::RUNTIME},
  };
  for (auto [s, t] : cases) {
    EXPECT_EQ(Component::stringToComponentType(s), t) << "input: " << s;
  }
}
TEST(ComponentType, UnknownStringReturnsUnknown) {
  EXPECT_EQ(Component::stringToComponentType("garbage"), Component::Type::UNKNOWN);
}
TEST(ComponentType, RoundTrip) {
  using T = Component::Type;
  for (auto t :
       {T::GENERIC, T::DESKTOP_APPLICATION, T::CONSOLE_APPLICATION, T::WEB_APPLICATION, T::ADDON,
        T::FONT, T::CODEC, T::INPUT_METHOD, T::FIRMWARE, T::DRIVER, T::LOCALIZATION, T::SERVICE,
        T::REPOSITORY, T::OPERATING_SYSTEM, T::ICON_THEME, T::RUNTIME}) {
    EXPECT_EQ(Component::stringToComponentType(Component::componentTypeToString(t)), t);
  }
}

// ── BundleType ────────────────────────────────────────────────────────────

TEST(BundleType, AllKnownTypes) {
  using B = Component::BundleType;
  const std::pair<std::string_view, B> cases[] = {
      {"package", B::PACKAGE},   {"limba", B::LIMBA},       {"flatpak", B::FLATPAK},
      {"appimage", B::APPIMAGE}, {"snap", B::SNAP},         {"tarball", B::TARBALL},
      {"cabinet", B::CABINET},   {"linglong", B::LINGLONG},
  };
  for (auto [s, b] : cases) {
    EXPECT_EQ(Component::stringToBundleType(s), b) << "input: " << s;
  }
}
TEST(BundleType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToBundleType("unknown-bundle"), Component::BundleType::UNKNOWN);
}
TEST(BundleType, RoundTrip) {
  using B = Component::BundleType;
  for (auto b : {B::PACKAGE, B::LIMBA, B::FLATPAK, B::APPIMAGE, B::SNAP, B::TARBALL, B::CABINET,
                 B::LINGLONG}) {
    EXPECT_EQ(Component::stringToBundleType(Component::bundleTypeToString(b)), b);
  }
}

// ── IconType ──────────────────────────────────────────────────────────────

TEST(IconType, AllKnownTypes) {
  using I = Component::IconType;
  EXPECT_EQ(Component::stringToIconType("stock"), I::STOCK);
  EXPECT_EQ(Component::stringToIconType("cached"), I::CACHED);
  EXPECT_EQ(Component::stringToIconType("local"), I::LOCAL);
  EXPECT_EQ(Component::stringToIconType("url"), I::URL);
  EXPECT_EQ(Component::stringToIconType("remote"), I::REMOTE);
}
TEST(IconType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToIconType("other"), Component::IconType::UNKNOWN);
}
TEST(IconType, RoundTrip) {
  using I = Component::IconType;
  for (auto i : {I::STOCK, I::CACHED, I::LOCAL, I::URL, I::REMOTE}) {
    EXPECT_EQ(Component::stringToIconType(Component::iconTypeToString(i)), i);
  }
}

// ── UrlType ───────────────────────────────────────────────────────────────

TEST(UrlType, AllKnownTypes) {
  using U = Component::UrlType;
  const std::pair<std::string_view, U> cases[] = {
      {"homepage", U::HOMEPAGE}, {"bugtracker", U::BUGTRACKER},   {"faq", U::FAQ},
      {"help", U::HELP},         {"donation", U::DONATION},       {"translate", U::TRANSLATE},
      {"contact", U::CONTACT},   {"vcs-browser", U::VCS_BROWSER}, {"contribute", U::CONTRIBUTE},
  };
  for (auto [s, u] : cases) {
    EXPECT_EQ(Component::stringToUrlType(s), u) << "input: " << s;
  }
}
TEST(UrlType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToUrlType("other"), Component::UrlType::UNKNOWN);
}
TEST(UrlType, RoundTrip) {
  using U = Component::UrlType;
  for (auto u : {U::HOMEPAGE, U::BUGTRACKER, U::FAQ, U::HELP, U::DONATION, U::TRANSLATE, U::CONTACT,
                 U::VCS_BROWSER, U::CONTRIBUTE}) {
    EXPECT_EQ(Component::stringToUrlType(Component::urlTypeToString(u)), u);
  }
}

// ── LaunchableType ────────────────────────────────────────────────────────

TEST(LaunchableType, AllKnownTypes) {
  using L = Component::LaunchableType;
  EXPECT_EQ(Component::stringToLaunchableType("desktop-id"), L::DESKTOP_ID);
  EXPECT_EQ(Component::stringToLaunchableType("service"), L::SERVICE);
  EXPECT_EQ(Component::stringToLaunchableType("cockpit-manifest"), L::COCKPIT_MANIFEST);
  EXPECT_EQ(Component::stringToLaunchableType("url"), L::URL);
}
TEST(LaunchableType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToLaunchableType("other"), Component::LaunchableType::UNKNOWN);
}

// ── ReleaseType ───────────────────────────────────────────────────────────

TEST(ReleaseType, AllKnownTypes) {
  using R = Component::ReleaseType;
  EXPECT_EQ(Component::stringToReleaseType("stable"), R::STABLE);
  EXPECT_EQ(Component::stringToReleaseType("development"), R::DEVELOPMENT);
  EXPECT_EQ(Component::stringToReleaseType("snapshot"), R::SNAPSHOT);
}
TEST(ReleaseType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToReleaseType("other"), Component::ReleaseType::UNKNOWN);
}
TEST(ReleaseType, RoundTrip) {
  using R = Component::ReleaseType;
  for (auto r : {R::STABLE, R::DEVELOPMENT, R::SNAPSHOT}) {
    EXPECT_EQ(Component::stringToReleaseType(Component::releaseTypeToString(r)), r);
  }
}

// ── ReleaseUrgency ────────────────────────────────────────────────────────

TEST(ReleaseUrgency, AllKnownTypes) {
  using U = Component::ReleaseUrgency;
  EXPECT_EQ(Component::stringToReleaseUrgency("low"), U::LOW);
  EXPECT_EQ(Component::stringToReleaseUrgency("medium"), U::MEDIUM);
  EXPECT_EQ(Component::stringToReleaseUrgency("high"), U::HIGH);
  EXPECT_EQ(Component::stringToReleaseUrgency("critical"), U::CRITICAL);
}
TEST(ReleaseUrgency, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToReleaseUrgency("other"), Component::ReleaseUrgency::UNKNOWN);
}
TEST(ReleaseUrgency, RoundTrip) {
  using U = Component::ReleaseUrgency;
  for (auto u : {U::LOW, U::MEDIUM, U::HIGH, U::CRITICAL}) {
    EXPECT_EQ(Component::stringToReleaseUrgency(Component::releaseUrgencyToString(u)), u);
  }
}

// ── IssueType ─────────────────────────────────────────────────────────────

TEST(IssueType, AllKnownTypes) {
  using I = Component::IssueType;
  EXPECT_EQ(Component::stringToIssueType("generic"), I::GENERIC);
  EXPECT_EQ(Component::stringToIssueType("cve"), I::CVE);
}
TEST(IssueType, UnknownReturnsUnknown) {
  EXPECT_EQ(Component::stringToIssueType("other"), Component::IssueType::UNKNOWN);
}
TEST(IssueType, RoundTrip) {
  using I = Component::IssueType;
  for (auto i : {I::GENERIC, I::CVE}) {
    EXPECT_EQ(Component::stringToIssueType(Component::issueTypeToString(i)), i);
  }
}

// ── setUrl / getUrl ───────────────────────────────────────────────────────

TEST(ComponentUrls, SetAndGetHomepage) {
  Component c;
  c.setUrl(Component::UrlType::HOMEPAGE, "https://example.com");
  EXPECT_EQ(c.getUrl(Component::UrlType::HOMEPAGE), "https://example.com");
}

TEST(ComponentUrls, GetUnsetTypeReturnsEmpty) {
  Component c;
  EXPECT_EQ(c.getUrl(Component::UrlType::BUGTRACKER), "");
}

TEST(ComponentUrls, SetOverwritesExistingUrl) {
  Component c;
  c.setUrl(Component::UrlType::HOMEPAGE, "https://a.com");
  c.setUrl(Component::UrlType::HOMEPAGE, "https://b.com");
  EXPECT_EQ(c.getUrl(Component::UrlType::HOMEPAGE), "https://b.com");
}

TEST(ComponentUrls, MultipleTypesIndependent) {
  Component c;
  c.setUrl(Component::UrlType::HOMEPAGE, "https://home.com");
  c.setUrl(Component::UrlType::BUGTRACKER, "https://bugs.com");
  c.setUrl(Component::UrlType::DONATION, "https://donate.com");
  EXPECT_EQ(c.getUrl(Component::UrlType::HOMEPAGE), "https://home.com");
  EXPECT_EQ(c.getUrl(Component::UrlType::BUGTRACKER), "https://bugs.com");
  EXPECT_EQ(c.getUrl(Component::UrlType::DONATION), "https://donate.com");
}

// ── addSupportedLanguage ──────────────────────────────────────────────────

TEST(ComponentLanguages, AddLanguageStoredCorrectly) {
  Component c;
  c.addSupportedLanguage("en");
  ASSERT_EQ(c.supportedLanguages.size(), 1u);
  EXPECT_EQ(c.supportedLanguages[0], "en");
}

TEST(ComponentLanguages, DeduplicatesOnAdd) {
  Component c;
  c.addSupportedLanguage("de");
  c.addSupportedLanguage("fr");
  c.addSupportedLanguage("de"); // duplicate
  // addSupportedLanguage does NOT deduplicate; all calls are appended
  EXPECT_EQ(c.supportedLanguages.size(), 3u);
}

TEST(ComponentLanguages, MultipleDistinctLanguages) {
  Component c;
  for (const auto &lang : {"en", "de", "fr", "ja", "zh"}) {
    c.addSupportedLanguage(lang);
  }
  EXPECT_EQ(c.supportedLanguages.size(), 5u);
}

// ── shrinkToFit ───────────────────────────────────────────────────────────

TEST(ComponentShrink, EmptyComponentDoesNotCrash) {
  Component c;
  EXPECT_NO_THROW(c.shrinkToFit());
}

TEST(ComponentShrink, DataPreservedAfterShrink) {
  Component c;
  c.id = "com.example.App";
  c.name = "Test";
  c.categories = {"Utility", "Science"};
  c.keywords = {"test", "example"};
  c.shrinkToFit();
  EXPECT_EQ(c.id, "com.example.App");
  EXPECT_EQ(c.name, "Test");
  EXPECT_EQ(c.categories.size(), 2u);
  EXPECT_EQ(c.keywords.size(), 2u);
}

// ── default field values ──────────────────────────────────────────────────

TEST(ComponentDefaults, TypeIsUnknown) {
  EXPECT_EQ(Component{}.type, Component::Type::UNKNOWN);
}
TEST(ComponentDefaults, PriorityIsZero) {
  EXPECT_EQ(Component{}.priority, 0);
}
TEST(ComponentDefaults, LaunchableTypeIsUnknown) {
  EXPECT_EQ(Component{}.launchable.type, Component::LaunchableType::UNKNOWN);
}
TEST(ComponentDefaults, BundleTypeIsUnknown) {
  EXPECT_EQ(Component{}.bundle.type, Component::BundleType::UNKNOWN);
}
TEST(ComponentDefaults, CollectionsAreEmpty) {
  Component c;
  EXPECT_TRUE(c.icons.empty());
  EXPECT_TRUE(c.categories.empty());
  EXPECT_TRUE(c.keywords.empty());
  EXPECT_TRUE(c.releases.empty());
  EXPECT_TRUE(c.screenshots.empty());
  EXPECT_TRUE(c.urls.empty());
}
