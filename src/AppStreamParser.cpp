// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

#include "AppStreamParser.h"

#include "../include/spdlog.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <unordered_set>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std::string_view_literals;

// ============================================================
// Helpers
// ============================================================

static int convertToInt(const std::string_view sv) {
  int result = 0;
  bool neg = false;
  size_t i = 0;
  if (!sv.empty() && sv[0] == '-') {
    neg = true;
    i = 1;
  }
  for (; i < sv.size(); ++i) {
    if (sv[i] < '0' || sv[i] > '9')
      break;
    result = (result * 10) + (sv[i] - '0');
  }
  return neg ? -result : result;
}

static size_t convertToSizeT(const std::string_view sv) {
  size_t result = 0;
  for (const char c : sv) {
    if (c < '0' || c > '9')
      break;
    result = (result * 10) + static_cast<size_t>(c - '0');
  }
  return result;
}

static std::string unixEpochToISO8601(const std::string_view epochStr) {
  long long epoch = 0;
  for (char c : epochStr) {
    if (c < '0' || c > '9')
      break;
    epoch = (epoch * 10) + (c - '0');
  }
  const auto t = static_cast<std::time_t>(epoch);
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::stringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << 'Z';
  return ss.str();
}

// ============================================================
// mmap helpers
// ============================================================

void AppStreamParser::mmapFile(const std::string &filename, void *&data, size_t &size) {
  data = nullptr;
  size = 0;
  const int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    spdlog::error("Failed to open: {}", filename);
    return;
  }
  struct stat sb{};
  if (fstat(fd, &sb) == -1) {
    close(fd);
    return;
  }
  size = static_cast<size_t>(sb.st_size);
  data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (data == MAP_FAILED) {
    data = nullptr;
    size = 0;
    return;
  }
  madvise(data, size, MADV_SEQUENTIAL);
}

void AppStreamParser::munmapFile(void *&data, size_t &size) {
  if (data && data != MAP_FAILED)
    munmap(data, size);
  data = nullptr;
  size = 0;
}

// ============================================================
// Attribute lookup
// ============================================================

std::string_view AppStreamParser::findAttr(const std::vector<XmlScanner::Attribute> &attrs,
                                           std::string_view name) {
  for (const auto &a : attrs) {
    if (a.name == name)
      return a.value;
  }
  return {};
}

// ============================================================
// InMemorySink
// ============================================================

namespace {

class InMemorySink final : public ComponentSink {
public:
  explicit InMemorySink(std::unordered_map<std::string, std::unique_ptr<Component>> &components)
      : components_(components), count_(0) {}

  std::expected<void, Error> onComponent(Component component) override {
    auto id = component.id;
    component.shrinkToFit();
    if (!components_.contains(id)) {
      components_.emplace(std::move(id), std::make_unique<Component>(std::move(component)));
      ++count_;
    } else {
      SPDLOG_WARN("Duplicate: [{}]", id);
    }
    return {};
  }

  [[nodiscard]] size_t componentCount() const override { return count_; }

private:
  std::unordered_map<std::string, std::unique_ptr<Component>> &components_;
  size_t count_;
};

} // anonymous namespace

// ============================================================
// Core parse loop
// ============================================================

std::expected<void, AppStreamParser::ParseError>
AppStreamParser::doParse(XmlScanner &scanner, const std::string &language, ComponentSink &sink) {

  // ---- Build language filter set ----
  // "" = default language only (elements with xml:lang are skipped)
  // "*" = all languages (every xml:lang element is kept)
  // "en,de,fr" = specific set (default + listed langs kept)
  bool keepAllLangs = (language == "*");
  std::unordered_set<std::string> langSet;
  if (!keepAllLangs && !language.empty()) {
    std::string_view sv(language);
    while (!sv.empty()) {
      auto comma = sv.find(',');
      auto token = sv.substr(0, comma);
      if (!token.empty())
        langSet.emplace(token);
      sv = (comma == std::string_view::npos) ? "" : sv.substr(comma + 1);
    }
  }
  const bool storeLangs = keepAllLangs || !langSet.empty();

  // ---- Parser state ----
  bool insideComponent = false;
  bool insideReleases = false;
  bool insideArtifact = false;
  bool insideDeveloper = false;
  bool insideScreenshots = false;
  bool insideScreenshot = false;
  bool insideContentRating = false;
  bool insideProvides = false;
  bool insideSuggests = false;
  bool insideBranding = false;
  bool insideRequires = false;
  bool insideRecommends = false;
  bool insideCustom = false;
  size_t releaseCount = 0;
  size_t screenshotIndex = 0;

  // Language skip depth — used when we want to skip a translation entirely
  int skipDepth = 0;

  // Current xml:lang of the element being parsed (empty = default)
  std::string currentLang;

  // Description passthrough: accumulate HTML markup inside <description>
  int descriptionDepth = 0;
  bool descInRelease = false;
  std::string descAccum;
  descAccum.reserve(512);
  std::string descLang; // xml:lang of current <description>

  std::string currentElement;
  currentElement.reserve(32);

  std::string textAccum;
  textAccum.reserve(256);

  // Root <components> attributes (inherited by child components)
  std::string rootMediaBaseurl;
  std::string rootArchitecture;

  // Temp objects
  Component currentComponent;
  Component::Release currentRelease;
  Component::Issue currentIssue;
  Component::Artifact currentArtifact;
  Component::Icon currentIcon;
  Component::Screenshot currentScreenshot;
  Component::Image currentImage;
  Component::Video currentVideo;
  Component::Relation currentRelation;
  Component::UrlType urlType = Component::UrlType::UNKNOWN;
  Component::LaunchableType launchableType = Component::LaunchableType::UNKNOWN;
  std::string currentArtifactChecksumKey;
  std::string currentArtifactSizeKey;
  std::string currentContentAttrId;
  std::string currentDbusType;
  std::string currentCustomKey;
  std::string currentBrandingScheme;
  std::string currentTranslationType;

  if (auto r = sink.begin(); !r) {
    return std::unexpected(ParseError::SINK_ERROR);
  }

  while (!scanner.atEnd()) {
    auto result = scanner.next();
    if (!result) {
      spdlog::error("XML scan error");
      return std::unexpected(ParseError::XML_PARSE_ERROR);
    }

    auto &evt = *result;

    switch (evt.type) {

    // ============================================================
    // START_ELEMENT
    // ============================================================
    case XmlScanner::EventType::START_ELEMENT: {

      // ---- Language skip ----
      if (skipDepth > 0) {
        ++skipDepth;
        break;
      }

      // ---- Description passthrough ----
      if (descriptionDepth > 0) {
        ++descriptionDepth;
        // Accumulate this tag as HTML markup
        descAccum.push_back('<');
        descAccum.append(evt.name.data(), evt.name.size());
        // Preserve href for <a> tags
        if (evt.name == "a"sv) {
          auto href = findAttr(evt.attributes, "href"sv);
          if (!href.empty()) {
            descAccum.append(" href=\"");
            descAccum.append(href.data(), href.size());
            descAccum.push_back('"');
          }
        }
        descAccum.push_back('>');
        break;
      }

      currentElement = evt.name;
      textAccum.clear();

      // ---- Root <components> tag ----
      if (evt.name == "components"sv) {
        rootMediaBaseurl = std::string(findAttr(evt.attributes, "media_baseurl"sv));
        rootArchitecture = std::string(findAttr(evt.attributes, "architecture"sv));
        break;
      }

      // ---- <component> ----
      if (evt.name == "component"sv) {
        insideComponent = true;
        currentComponent = Component{};
        releaseCount = 0;
        screenshotIndex = 0;
        currentLang.clear();

        // Parse type and priority attributes
        auto typeAttr = findAttr(evt.attributes, "type"sv);
        if (!typeAttr.empty()) {
          currentComponent.type = Component::stringToComponentType(typeAttr);
        }
        auto priorityAttr = findAttr(evt.attributes, "priority"sv);
        if (!priorityAttr.empty()) {
          currentComponent.priority = convertToInt(priorityAttr);
        }
        auto mergeAttr = findAttr(evt.attributes, "merge"sv);
        if (!mergeAttr.empty()) {
          currentComponent.merge = std::string(mergeAttr);
        }

        // Inherit root attributes
        if (!rootMediaBaseurl.empty()) {
          currentComponent.media_baseurl = rootMediaBaseurl;
        }
        if (!rootArchitecture.empty()) {
          currentComponent.architecture = rootArchitecture;
        }
        break;
      }

      if (!insideComponent)
        break;

      // ---- <description> — enter passthrough mode ----
      if (evt.name == "description"sv) {
        auto lang = findAttr(evt.attributes, "xml:lang"sv);
        if (!lang.empty()) {
          // Non-default language: skip if not in our set
          if (!storeLangs || (!keepAllLangs && !langSet.contains(std::string(lang)))) {
            skipDepth = 1;
            break;
          }
        }
        descLang = std::string(lang);
        descriptionDepth = 1;
        descInRelease = insideReleases;
        descAccum.clear();
        break;
      }

      // ---- <releases> subtree ----
      if (evt.name == "releases"sv) {
        insideReleases = true;
        break;
      }

      if (insideReleases) {
        if (evt.name == "release"sv) {
          currentRelease = Component::Release{};
          currentRelease.type = Component::ReleaseType::STABLE;
          currentRelease.urgency = Component::ReleaseUrgency::MEDIUM;
          for (const auto &a : evt.attributes) {
            if (a.name == "type"sv)
              currentRelease.type = Component::stringToReleaseType(a.value);
            else if (a.name == "version"sv)
              currentRelease.version = std::string(a.value);
            else if (a.name == "date"sv)
              currentRelease.date = std::string(a.value);
            else if (a.name == "timestamp"sv)
              currentRelease.timestamp = unixEpochToISO8601(a.value);
            else if (a.name == "date_eol"sv)
              currentRelease.date_eol = std::string(a.value);
            else if (a.name == "urgency"sv)
              currentRelease.urgency = Component::stringToReleaseUrgency(a.value);
          }
          break;
        }
        if (evt.name == "issues"sv) {
          break;
        }
        if (evt.name == "issue"sv) {
          currentIssue = Component::Issue{};
          for (const auto &a : evt.attributes) {
            if (a.name == "type"sv)
              currentIssue.type = Component::stringToIssueType(a.value);
            else if (a.name == "url"sv)
              currentIssue.url = std::string(a.value);
          }
          break;
        }
        if (evt.name == "artifact"sv) {
          insideArtifact = true;
          currentArtifact = {};
          break;
        }
        if (insideArtifact) {
          if (evt.name == "checksum"sv) {
            currentArtifactChecksumKey = std::string(findAttr(evt.attributes, "type"sv));
          } else if (evt.name == "size"sv) {
            currentArtifactSizeKey = std::string(findAttr(evt.attributes, "type"sv));
          }
          break;
        }
        // Note: <description> inside <release> is handled above
        break; // don't fall through to general handler while in releases
      }

      // ---- <screenshots> ----
      if (evt.name == "screenshots"sv) {
        insideScreenshots = true;
        break;
      }
      if (insideScreenshots) {
        if (evt.name == "screenshot"sv) {
          insideScreenshot = true;
          currentScreenshot = Component::Screenshot{};
          auto typeAttr = findAttr(evt.attributes, "type"sv);
          if (typeAttr == "default"sv)
            currentScreenshot.isDefault = true;
          break;
        }
        if (insideScreenshot) {
          if (evt.name == "image"sv) {
            currentImage = Component::Image{};
            currentImage.type = std::string(findAttr(evt.attributes, "type"sv));
            auto w = findAttr(evt.attributes, "width"sv);
            auto h = findAttr(evt.attributes, "height"sv);
            if (!w.empty())
              currentImage.width = convertToInt(w);
            if (!h.empty())
              currentImage.height = convertToInt(h);
            break;
          }
          if (evt.name == "video"sv) {
            currentVideo = Component::Video{};
            currentVideo.codec = std::string(findAttr(evt.attributes, "codec"sv));
            currentVideo.container = std::string(findAttr(evt.attributes, "container"sv));
            auto w = findAttr(evt.attributes, "width"sv);
            auto h = findAttr(evt.attributes, "height"sv);
            if (!w.empty())
              currentVideo.width = convertToInt(w);
            if (!h.empty())
              currentVideo.height = convertToInt(h);
            break;
          }
          if (evt.name == "caption"sv) {
            auto lang = findAttr(evt.attributes, "xml:lang"sv);
            if (!lang.empty()) {
              if (!storeLangs || (!keepAllLangs && !langSet.contains(std::string(lang)))) {
                skipDepth = 1;
                break;
              }
            }
            currentLang = std::string(lang);
            break;
          }
        }
        break;
      }

      // ---- <content_rating> ----
      if (evt.name == "content_rating"sv) {
        insideContentRating = true;
        currentComponent.content_rating.type = std::string(findAttr(evt.attributes, "type"sv));
        break;
      }
      if (insideContentRating && evt.name == "content_attribute"sv) {
        currentContentAttrId = std::string(findAttr(evt.attributes, "id"sv));
        break;
      }

      // ---- <provides> ----
      if (evt.name == "provides"sv) {
        insideProvides = true;
        break;
      }
      if (insideProvides) {
        if (evt.name == "dbus"sv) {
          currentDbusType = std::string(findAttr(evt.attributes, "type"sv));
        } else if (evt.name == "firmware"sv) {
          // firmware can have type="runtime" or type="flashed"
          auto ft = findAttr(evt.attributes, "type"sv);
          if (!ft.empty())
            currentDbusType = std::string(ft); // reuse temp
        }
        break;
      }

      // ---- <suggests> ----
      if (evt.name == "suggests"sv) {
        insideSuggests = true;
        break;
      }

      // ---- <branding> ----
      if (evt.name == "branding"sv) {
        insideBranding = true;
        break;
      }
      if (insideBranding && evt.name == "color"sv) {
        currentBrandingScheme = std::string(findAttr(evt.attributes, "scheme_preference"sv));
        break;
      }

      // ---- <requires> / <recommends> ----
      if (evt.name == "requires"sv) {
        insideRequires = true;
        break;
      }
      if (evt.name == "recommends"sv) {
        insideRecommends = true;
        break;
      }
      if (insideRequires || insideRecommends) {
        currentRelation = Component::Relation{};
        currentRelation.type = std::string(evt.name);
        currentRelation.compare = std::string(findAttr(evt.attributes, "compare"sv));
        currentRelation.version = std::string(findAttr(evt.attributes, "version"sv));
        break;
      }

      // ---- <custom> ----
      if (evt.name == "custom"sv) {
        insideCustom = true;
        break;
      }
      if (insideCustom && evt.name == "value"sv) {
        currentCustomKey = std::string(findAttr(evt.attributes, "key"sv));
        break;
      }

      // ---- <icon> ----
      if (evt.name == "icon"sv) {
        currentIcon = Component::Icon{};
        for (const auto &a : evt.attributes) {
          if (a.name == "type"sv)
            currentIcon.type = Component::stringToIconType(a.value);
          else if (a.name == "width"sv)
            currentIcon.width = convertToInt(a.value);
          else if (a.name == "height"sv)
            currentIcon.height = convertToInt(a.value);
          else if (a.name == "scale"sv)
            currentIcon.scale = convertToInt(a.value);
        }
        break;
      }

      // ---- <translation> ----
      if (evt.name == "translation"sv) {
        currentTranslationType = std::string(findAttr(evt.attributes, "type"sv));
        break;
      }

      // ---- Language check for remaining translatable elements ----
      {
        auto lang = findAttr(evt.attributes, "xml:lang"sv);
        if (!lang.empty()) {
          if (!storeLangs || (!keepAllLangs && !langSet.contains(std::string(lang)))) {
            skipDepth = 1;
            break;
          }
          currentLang = std::string(lang);
        }
      }

      // ---- Tag-specific attributes ----
      if (evt.name == "developer"sv) {
        auto devId = findAttr(evt.attributes, "id"sv);
        if (!devId.empty())
          currentComponent.developer.id = std::string(devId);
        insideDeveloper = true;
      } else if (evt.name == "bundle"sv) {
        auto t = findAttr(evt.attributes, "type"sv);
        if (!t.empty())
          currentComponent.bundle.type = Component::stringToBundleType(t);
      } else if (evt.name == "url"sv) {
        auto t = findAttr(evt.attributes, "type"sv);
        if (!t.empty())
          urlType = Component::stringToUrlType(t);
      } else if (evt.name == "launchable"sv) {
        auto t = findAttr(evt.attributes, "type"sv);
        if (!t.empty())
          launchableType = Component::stringToLaunchableType(t);
      }
      break;
    }

    // ============================================================
    // TEXT
    // ============================================================
    case XmlScanner::EventType::TEXT: {
      if (skipDepth > 0)
        break;
      if (descriptionDepth > 0) {
        auto t = evt.text();
        descAccum.append(t.data(), t.size());
        break;
      }
      if (currentElement.empty())
        break;
      auto t = evt.text();
      textAccum.append(t.data(), t.size());
      break;
    }

    // ============================================================
    // END_ELEMENT
    // ============================================================
    case XmlScanner::EventType::END_ELEMENT: {

      // ---- Language skip ----
      if (skipDepth > 0) {
        --skipDepth;
        if (skipDepth == 0) {
          currentElement.clear();
          textAccum.clear();
        }
        break;
      }

      // ---- Description passthrough ----
      if (descriptionDepth > 0) {
        --descriptionDepth;
        if (descriptionDepth == 0) {
          // </description> — assign accumulated HTML
          if (descLang.empty()) {
            // Default language — store in main field
            if (descInRelease) {
              currentRelease.description = std::move(descAccum);
            } else {
              currentComponent.description = std::move(descAccum);
            }
          } else {
            // Translation — store in field_translations
            std::string field = descInRelease ? "release_description" : "description";
            currentComponent.field_translations.push_back(
                {std::move(field), std::move(descLang), std::move(descAccum)});
          }
          descAccum.clear();
          descLang.clear();
        } else {
          // Close an HTML tag inside description
          descAccum.append("</");
          descAccum.append(evt.name.data(), evt.name.size());
          descAccum.push_back('>');
        }
        break;
      }

      if (!insideComponent)
        break;

      auto tag = evt.name;

      // ---- </component> ----
      if (tag == "component"sv) {
        insideComponent = false;
        if (!currentComponent.id.empty()) {
          auto r = sink.onComponent(std::move(currentComponent));
          currentComponent = {}; // clear moved-from state
          if (!r)
            return std::unexpected(ParseError::SINK_ERROR);
        }
        break;
      }

      // ---- Releases subtree ----
      if (tag == "releases"sv) {
        insideReleases = false;
        break;
      }
      if (insideReleases || tag == "release"sv) {
        if (tag == "release"sv) {
          if (releaseCount < kMaxReleasesPerComponent) {
            currentComponent.releases.push_back(std::move(currentRelease));
            ++releaseCount;
          }
          currentRelease = {};
        } else if (tag == "issue"sv) {
          currentIssue.value = std::move(textAccum);
          currentRelease.issues.push_back(std::move(currentIssue));
          currentIssue = {};
        } else if (tag == "artifact"sv) {
          insideArtifact = false;
          currentRelease.artifacts.push_back(std::move(currentArtifact));
          currentArtifact = {};
        } else if (insideArtifact && tag == "location"sv) {
          currentArtifact.location = std::move(textAccum);
        } else if (insideArtifact && tag == "checksum"sv) {
          if (!currentArtifactChecksumKey.empty()) {
            currentArtifact.checksums.emplace_back(std::move(currentArtifactChecksumKey),
                                                   std::move(textAccum));
            currentArtifactChecksumKey.clear();
          }
        } else if (insideArtifact && tag == "size"sv) {
          if (!currentArtifactSizeKey.empty()) {
            currentArtifact.sizes.emplace_back(std::move(currentArtifactSizeKey),
                                               convertToSizeT(textAccum));
            currentArtifactSizeKey.clear();
          }
        } else if (tag == "url"sv) {
          currentRelease.url = std::move(textAccum);
        }
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Screenshots ----
      if (tag == "screenshots"sv) {
        insideScreenshots = false;
        break;
      }
      if (insideScreenshots) {
        if (tag == "screenshot"sv) {
          insideScreenshot = false;
          currentComponent.screenshots.push_back(std::move(currentScreenshot));
          currentScreenshot = {};
        } else if (tag == "image"sv) {
          currentImage.url = std::move(textAccum);
          currentScreenshot.images.push_back(std::move(currentImage));
          currentImage = {};
        } else if (tag == "video"sv) {
          currentVideo.url = std::move(textAccum);
          currentScreenshot.videos.push_back(std::move(currentVideo));
          currentVideo = {};
        } else if (tag == "caption"sv) {
          if (currentLang.empty()) {
            currentScreenshot.caption = std::move(textAccum);
          } else {
            currentComponent.field_translations.push_back(
                {"caption:" + std::to_string(screenshotIndex), std::move(currentLang),
                 std::move(textAccum)});
          }
        }
        if (tag == "screenshot"sv)
          ++screenshotIndex;
        currentElement.clear();
        textAccum.clear();
        currentLang.clear();
        break;
      }

      // ---- Content rating ----
      if (tag == "content_rating"sv) {
        insideContentRating = false;
        break;
      }
      if (insideContentRating && tag == "content_attribute"sv) {
        if (!currentContentAttrId.empty()) {
          currentComponent.content_rating.attributes.emplace_back(std::move(currentContentAttrId),
                                                                  std::move(textAccum));
          currentContentAttrId.clear();
        }
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Provides ----
      if (tag == "provides"sv) {
        insideProvides = false;
        break;
      }
      if (insideProvides) {
        if (tag == "binary"sv) {
          currentComponent.provides.binaries.push_back(std::move(textAccum));
        } else if (tag == "library"sv) {
          currentComponent.provides.libraries.push_back(std::move(textAccum));
        } else if (tag == "mediatype"sv) {
          currentComponent.provides.mediatypes.push_back(std::move(textAccum));
        } else if (tag == "id"sv) {
          currentComponent.provides.ids.push_back(std::move(textAccum));
        } else if (tag == "dbus"sv) {
          currentComponent.provides.dbus.emplace_back(std::move(currentDbusType),
                                                      std::move(textAccum));
          currentDbusType.clear();
        } else if (tag == "firmware"sv) {
          currentComponent.provides.firmware.push_back(std::move(textAccum));
        }
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Suggests ----
      if (tag == "suggests"sv) {
        insideSuggests = false;
        break;
      }
      if (insideSuggests && tag == "id"sv) {
        currentComponent.suggests_ids.push_back(std::move(textAccum));
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Branding ----
      if (tag == "branding"sv) {
        insideBranding = false;
        break;
      }
      if (insideBranding && tag == "color"sv) {
        currentComponent.branding_colors.push_back(
            {std::move(currentBrandingScheme), std::move(textAccum)});
        currentBrandingScheme.clear();
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Requires / Recommends ----
      if (tag == "requires"sv) {
        insideRequires = false;
        break;
      }
      if (tag == "recommends"sv) {
        insideRecommends = false;
        break;
      }
      if (insideRequires || insideRecommends) {
        currentRelation.value = std::move(textAccum);
        if (insideRequires)
          currentComponent.requires_.push_back(std::move(currentRelation));
        else
          currentComponent.recommends.push_back(std::move(currentRelation));
        currentRelation = {};
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Custom ----
      if (tag == "custom"sv) {
        insideCustom = false;
        break;
      }
      if (insideCustom && tag == "value"sv) {
        if (!currentCustomKey.empty()) {
          currentComponent.custom.emplace_back(std::move(currentCustomKey), std::move(textAccum));
          currentCustomKey.clear();
        }
        currentElement.clear();
        textAccum.clear();
        break;
      }

      // ---- Simple fields ----
      if (tag == "id"sv) {
        currentComponent.id = std::move(textAccum);
      } else if (tag == "pkgname"sv) {
        currentComponent.pkgname = std::move(textAccum);
      } else if (tag == "source_pkgname"sv) {
        currentComponent.source_pkgname = std::move(textAccum);
      } else if (tag == "name"sv) {
        if (!currentLang.empty()) {
          std::string field = insideDeveloper ? "developer_name" : "name";
          currentComponent.field_translations.push_back({field, currentLang, textAccum});
        }
        if (insideDeveloper)
          currentComponent.developer.name = std::move(textAccum);
        else
          currentComponent.name = std::move(textAccum);
      } else if (tag == "name_variant_suffix"sv) {
        currentComponent.name_variant_suffix = std::move(textAccum);
      } else if (tag == "project_license"sv) {
        currentComponent.projectLicense = std::move(textAccum);
      } else if (tag == "metadata_license"sv) {
        currentComponent.metadata_license = std::move(textAccum);
      } else if (tag == "summary"sv) {
        if (!currentLang.empty()) {
          currentComponent.field_translations.push_back({"summary", currentLang, textAccum});
        }
        currentComponent.summary = std::move(textAccum);
      } else if (tag == "url"sv) {
        currentComponent.setUrl(urlType, std::move(textAccum));
      } else if (tag == "project_group"sv) {
        currentComponent.project_group = std::move(textAccum);
      } else if (tag == "compulsory_for_desktop"sv) {
        currentComponent.compulsory_for_desktop.push_back(
            Component::stringToCompulsoryForDesktop(textAccum));
      } else if (tag == "developer"sv) {
        insideDeveloper = false;
      } else if (tag == "launchable"sv) {
        currentComponent.launchable.type = launchableType;
        currentComponent.launchable.value = std::move(textAccum);
      } else if (tag == "bundle"sv) {
        currentComponent.bundle.id = std::move(textAccum);
      } else if (tag == "keyword"sv) {
        currentComponent.keywords.push_back(textAccum);
      } else if (tag == "category"sv) {
        currentComponent.categories.push_back(textAccum);
      } else if (tag == "icon"sv) {
        currentIcon.value = std::move(textAccum);
        currentComponent.icons.push_back(std::move(currentIcon));
        currentIcon = {};
      } else if (tag == "extends"sv) {
        currentComponent.extends.push_back(std::move(textAccum));
      } else if (tag == "replaces"sv) {
        // <replaces> contains <id> children, but some catalogs
        // emit it as a leaf. Handle both.
      } else if (tag == "media_baseurl"sv) {
        currentComponent.media_baseurl = std::move(textAccum);
      } else if (tag == "architecture"sv) {
        currentComponent.architecture = std::move(textAccum);
      } else if (tag == "agreement"sv) {
        currentComponent.agreement = std::move(textAccum);
      } else if (tag == "translation"sv) {
        currentComponent.translations.emplace_back(std::move(currentTranslationType),
                                                   std::move(textAccum));
        currentTranslationType.clear();
      } else if (tag == "language"sv) {
        currentComponent.addSupportedLanguage(textAccum);
      }

      currentElement.clear();
      textAccum.clear();
      currentLang.clear();
      break;
    }

    case XmlScanner::EventType::END_OF_DOCUMENT:
      goto done;

    case XmlScanner::EventType::ERROR:
      spdlog::error("XML scan error");
      return std::unexpected(ParseError::XML_PARSE_ERROR);
    }
  }

done:
  if (auto r = sink.end(); !r) {
    return std::unexpected(ParseError::SINK_ERROR);
  }
  return {};
}

// ============================================================
// parseToSink (streaming)
// ============================================================

std::expected<void, AppStreamParser::ParseError>
AppStreamParser::parseToSink(const std::string &filename, const std::string &language,
                             ComponentSink &sink) {
  const int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1)
    return std::unexpected(ParseError::MMAP_FAILED);

  // RAII guard ensures fd is closed even if doParse throws
  struct FdGuard {
    int fd;
    ~FdGuard() { close(fd); }
  } guard{fd};

  spdlog::info("Parsing file (streaming): {}", filename);
  XmlScanner scanner(fd);
  return doParse(scanner, language, sink);
}

// ============================================================
// create (in-memory)
// ============================================================

std::expected<AppStreamParser, AppStreamParser::ParseError>
AppStreamParser::create(const std::string &filename, const std::string &language) {
  AppStreamParser parser;
  parser.language_ = language;
  mmapFile(filename, parser.fileData_, parser.fileSize_);
  if (!parser.fileData_)
    return std::unexpected(ParseError::MMAP_FAILED);
  spdlog::info("Parsing file (in-memory): {}", filename);
  InMemorySink sink(parser.components_);
  XmlScanner scanner(static_cast<const char *>(parser.fileData_), parser.fileSize_);
  auto result = doParse(scanner, language, sink);
  munmapFile(parser.fileData_, parser.fileSize_);
  if (!result)
    return std::unexpected(result.error());
  return parser;
}

// ============================================================
// Lifecycle
// ============================================================

AppStreamParser::~AppStreamParser() {
  components_.clear();
  munmapFile(fileData_, fileSize_);
}

AppStreamParser::AppStreamParser(AppStreamParser &&o) noexcept
    : components_(std::move(o.components_)), language_(std::move(o.language_)),
      fileSize_(o.fileSize_), fileData_(o.fileData_) {
  o.fileData_ = nullptr;
  o.fileSize_ = 0;
}

AppStreamParser &AppStreamParser::operator=(AppStreamParser &&o) noexcept {
  if (this != &o) {
    components_.clear();
    munmapFile(fileData_, fileSize_);
    components_ = std::move(o.components_);
    language_ = std::move(o.language_);
    fileSize_ = o.fileSize_;
    fileData_ = o.fileData_;
    o.fileData_ = nullptr;
    o.fileSize_ = 0;
  }
  return *this;
}

// ============================================================
// Query methods
// ============================================================

std::vector<std::string> AppStreamParser::getUniqueCategories() const {
  std::unordered_set<std::string> u;
  for (const auto &c : components_ | std::views::values)
    u.insert(c->categories.begin(), c->categories.end());
  return {u.begin(), u.end()};
}

std::vector<std::string> AppStreamParser::getUniqueKeywords() const {
  std::unordered_set<std::string> u;
  for (const auto &c : components_ | std::views::values)
    u.insert(c->keywords.begin(), c->keywords.end());
  return {u.begin(), u.end()};
}

std::vector<const Component *> AppStreamParser::getSortedComponents(SortOption option) const {
  std::vector<const Component *> s;
  s.reserve(components_.size());
  for (const auto &c : components_ | std::views::values)
    s.push_back(c.get());
  switch (option) {
  case SortOption::BY_ID:
    std::ranges::sort(s, {}, &Component::id);
    break;
  case SortOption::BY_NAME:
    std::ranges::sort(s, {}, &Component::name);
    break;
  }
  return s;
}

std::vector<const Component *> AppStreamParser::searchByCategory(std::string_view cat) const {
  std::vector<const Component *> r;
  for (const auto &c : components_ | std::views::values)
    if (std::ranges::contains(c->categories, cat))
      r.push_back(c.get());
  return r;
}

std::vector<const Component *> AppStreamParser::searchByKeyword(std::string_view keyword) const {
  std::vector<const Component *> r;
  for (const auto &c : components_ | std::views::values)
    if (std::ranges::contains(c->keywords, keyword))
      r.push_back(c.get());
  return r;
}

size_t AppStreamParser::getTotalComponentCount() const {
  return components_.size();
}

const Component *AppStreamParser::findComponent(const std::string &id) const {
  auto it = components_.find(id);
  return it != components_.end() ? it->second.get() : nullptr;
}