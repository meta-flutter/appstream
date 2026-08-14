// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Joel Winarske <joel.winarske@gmail.com>

#include "XmlScanner.h"

#include <cstring>
#include <unistd.h>

XmlScanner::XmlScanner(const char *data, size_t size)
    : pos_(data), end_(data + size), data_(data), pending_end_(false) {
  attr_buf_.reserve(8);
  decode_buf_.reserve(256);

  // Skip BOM if present
  if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
    pos_ += 3;
  }

  // Skip XML declaration if present
  skipWhitespace();
  if (pos_ + 5 < end_ && pos_[0] == '<' && pos_[1] == '?') {
    skipXmlDeclaration();
  }
}

XmlScanner::XmlScanner(int fd, size_t bufSize)
    : pos_(nullptr), end_(nullptr), data_(nullptr), fd_(fd), stream_buf_(bufSize),
      pending_end_(false) {
  attr_buf_.reserve(8);
  decode_buf_.reserve(256);

  // Initial fill
  ssize_t n = ::read(fd_, stream_buf_.data(), stream_buf_.size());
  if (n <= 0) {
    pos_ = end_ = data_ = stream_buf_.data();
    eof_ = true;
    return;
  }
  data_ = stream_buf_.data();
  pos_ = stream_buf_.data();
  end_ = stream_buf_.data() + n;

  // Skip BOM
  const auto remaining = static_cast<size_t>(end_ - pos_);
  if (remaining >= 3 && static_cast<unsigned char>(pos_[0]) == 0xEF &&
      static_cast<unsigned char>(pos_[1]) == 0xBB && static_cast<unsigned char>(pos_[2]) == 0xBF) {
    pos_ += 3;
  }

  // Skip XML declaration
  skipWhitespace();
  if (pos_ + 5 < end_ && pos_[0] == '<' && pos_[1] == '?') {
    skipXmlDeclaration();
  }
}

void XmlScanner::refillIfNeeded() {
  if (fd_ < 0 || eof_)
    return;

  const auto remaining = static_cast<size_t>(end_ - pos_);
  // Refill when less than half the buffer remains
  if (remaining >= stream_buf_.size() / 2)
    return;

  // Compact: move unconsumed data to the front
  if (remaining > 0 && pos_ != stream_buf_.data()) {
    memmove(stream_buf_.data(), pos_, remaining);
  }
  pos_ = stream_buf_.data();
  end_ = stream_buf_.data() + remaining;
  data_ = stream_buf_.data();

  // Fill the rest of the buffer
  const size_t space = stream_buf_.size() - remaining;
  ssize_t n = ::read(fd_, stream_buf_.data() + remaining, space);
  if (n <= 0) {
    eof_ = true;
  } else {
    end_ = stream_buf_.data() + remaining + static_cast<size_t>(n);
  }
}

void XmlScanner::skipWhitespace() {
  while (pos_ < end_ && (*pos_ == ' ' || *pos_ == '\t' || *pos_ == '\n' || *pos_ == '\r')) {
    ++pos_;
  }
}

void XmlScanner::skipXmlDeclaration() {
  // Skip <?xml ... ?>
  while (pos_ + 1 < end_) {
    if (pos_[0] == '?' && pos_[1] == '>') {
      pos_ += 2;
      return;
    }
    ++pos_;
  }
}

void XmlScanner::skipComment() {
  // pos_ is at '<', skip <!-- ... -->
  pos_ += 4; // skip <!--
  while (pos_ + 2 < end_) {
    if (pos_[0] == '-' && pos_[1] == '-' && pos_[2] == '>') {
      pos_ += 3;
      return;
    }
    ++pos_;
  }
  pos_ = end_;
}

bool XmlScanner::containsEntity(std::string_view sv) noexcept {
  return sv.contains('&');
}

void XmlScanner::decodeEntities(std::string_view src) {
  decode_buf_.clear();
  decode_buf_.reserve(src.size());

  const char *p = src.data();
  const char *e = p + src.size();

  while (p < e) {
    if (*p != '&') {
      decode_buf_ += *p++;
      continue;
    }

    // Find the ';'
    const char *semi = static_cast<const char *>(memchr(p, ';', static_cast<size_t>(e - p)));
    if (!semi) {
      // Malformed entity, just copy the '&'
      decode_buf_ += *p++;
      continue;
    }

    std::string_view entity(p + 1, static_cast<size_t>(semi - p - 1));

    if (entity == "amp") {
      decode_buf_ += '&';
    } else if (entity == "lt") {
      decode_buf_ += '<';
    } else if (entity == "gt") {
      decode_buf_ += '>';
    } else if (entity == "quot") {
      decode_buf_ += '"';
    } else if (entity == "apos") {
      decode_buf_ += '\'';
    } else if (entity.size() > 1 && entity[0] == '#') {
      // Numeric character reference
      unsigned long cp = 0;
      bool valid = true;
      if (entity[1] == 'x' || entity[1] == 'X') {
        for (size_t i = 2; i < entity.size() && valid; ++i) {
          char c = entity[i];
          if (c >= '0' && c <= '9')
            cp = (cp * 16) + static_cast<unsigned long>(c - '0');
          else if (c >= 'a' && c <= 'f')
            cp = (cp * 16) + static_cast<unsigned long>(c - 'a' + 10);
          else if (c >= 'A' && c <= 'F')
            cp = (cp * 16) + static_cast<unsigned long>(c - 'A' + 10);
          else
            valid = false;
          if (cp > 0x10FFFF)
            valid = false;
        }
      } else {
        for (size_t i = 1; i < entity.size() && valid; ++i) {
          if (entity[i] >= '0' && entity[i] <= '9')
            cp = (cp * 10) + static_cast<unsigned long>(entity[i] - '0');
          else
            valid = false;
          if (cp > 0x10FFFF)
            valid = false;
        }
      }
      // Reject surrogate halves, NUL, and out-of-range codepoints
      if (!valid || cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        // Replace invalid reference with U+FFFD
        decode_buf_ += "\xEF\xBF\xBD";
      } else if (cp < 0x80) {
        decode_buf_ += static_cast<char>(cp);
      } else if (cp < 0x800) {
        decode_buf_ += static_cast<char>(0xC0 | (cp >> 6));
        decode_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        decode_buf_ += static_cast<char>(0xE0 | (cp >> 12));
        decode_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        decode_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        decode_buf_ += static_cast<char>(0xF0 | (cp >> 18));
        decode_buf_ += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        decode_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        decode_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
      }
    } else {
      // Unknown entity, copy verbatim
      decode_buf_ += '&';
      decode_buf_.append(entity.data(), entity.size());
      decode_buf_ += ';';
    }

    p = semi + 1;
  }
}

std::expected<XmlScanner::Event, XmlScanner::Error> XmlScanner::next() {
  // In streaming mode, ensure we have enough buffered data before parsing
  // the next token.  All string_views from the previous next() call are
  // consumed by now, so compacting the buffer is safe.
  refillIfNeeded();

  Event evt{};

  // Emit the pending END_ELEMENT for a self-closing tag from the previous call
  if (pending_end_) {
    pending_end_ = false;
    evt.type = EventType::END_ELEMENT;
    evt.name = pending_end_name_;
    return evt;
  }

  // Skip whitespace between tags (inter-element whitespace)
  // But we need to check for text content too

  if (pos_ >= end_) {
    evt.type = EventType::END_OF_DOCUMENT;
    return evt;
  }

  // If we're not at a '<', we have text content
  if (*pos_ != '<') {
    const char *text_start = pos_;
    while (pos_ < end_ && *pos_ != '<') {
      ++pos_;
    }

    std::string_view raw(text_start, static_cast<size_t>(pos_ - text_start));

    // Trim leading/trailing whitespace from text
    // (common in XML between tags)
    size_t start = 0;
    while (start < raw.size() &&
           (raw[start] == ' ' || raw[start] == '\t' || raw[start] == '\n' || raw[start] == '\r')) {
      ++start;
    }
    size_t end = raw.size();
    while (end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t' || raw[end - 1] == '\n' ||
                           raw[end - 1] == '\r')) {
      --end;
    }

    raw = raw.substr(start, end - start);

    if (raw.empty()) {
      // Pure whitespace, skip and get next event
      return next();
    }

    evt.type = EventType::TEXT;
    if (containsEntity(raw)) {
      decodeEntities(raw);
      evt.text_owned = decode_buf_;
      evt.text_has_entities = true;
    } else {
      evt.text_view = raw;
      evt.text_has_entities = false;
    }
    return evt;
  }

  // We're at '<'
  if (pos_ + 1 >= end_) {
    return std::unexpected(Error::UNEXPECTED_EOF);
  }

  // Comment: <!-- ... -->
  if (pos_ + 3 < end_ && pos_[1] == '!' && pos_[2] == '-' && pos_[3] == '-') {
    skipComment();
    return next();
  }

  // CDATA: <![CDATA[ ... ]]>  (rare in appstream but handle gracefully)
  if (pos_ + 8 < end_ && pos_[1] == '!' && pos_[2] == '[' && pos_[3] == 'C' && pos_[4] == 'D' &&
      pos_[5] == 'A' && pos_[6] == 'T' && pos_[7] == 'A' && pos_[8] == '[') {
    pos_ += 9;
    const char *cdata_start = pos_;
    while (pos_ + 2 < end_) {
      if (pos_[0] == ']' && pos_[1] == ']' && pos_[2] == '>') {
        evt.type = EventType::TEXT;
        evt.text_view = std::string_view(cdata_start, static_cast<size_t>(pos_ - cdata_start));
        evt.text_has_entities = false;
        pos_ += 3;
        return evt;
      }
      ++pos_;
    }
    return std::unexpected(Error::UNEXPECTED_EOF);
  }

  // End tag: </name>
  if (pos_[1] == '/') {
    pos_ += 2; // skip '</'
    const char *name_start = pos_;
    while (pos_ < end_ && *pos_ != '>') {
      ++pos_;
    }
    evt.type = EventType::END_ELEMENT;
    // Trim whitespace from tag name
    const char *name_end = pos_;
    while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t' ||
                                     name_end[-1] == '\n' || name_end[-1] == '\r')) {
      --name_end;
    }
    evt.name = std::string_view(name_start, static_cast<size_t>(name_end - name_start));
    if (pos_ < end_)
      ++pos_; // skip '>'
    return evt;
  }

  // Start tag: <name attr="val" ...> or self-closing <name ... />
  ++pos_; // skip '<'
  const char *name_start = pos_;
  while (pos_ < end_ && *pos_ != ' ' && *pos_ != '\t' && *pos_ != '\n' && *pos_ != '\r' &&
         *pos_ != '>' && *pos_ != '/') {
    ++pos_;
  }

  evt.type = EventType::START_ELEMENT;
  evt.name = std::string_view(name_start, static_cast<size_t>(pos_ - name_start));

  // Parse attributes
  attr_buf_.clear();

  while (pos_ < end_) {
    skipWhitespace();

    if (pos_ >= end_) {
      return std::unexpected(Error::UNEXPECTED_EOF);
    }

    // Self-closing tag
    if (*pos_ == '/') {
      ++pos_;
      if (pos_ < end_ && *pos_ == '>') {
        ++pos_;
      }
      evt.attributes = attr_buf_;
      // Schedule an END_ELEMENT event on the next call to next()
      pending_end_ = true;
      pending_end_name_ = evt.name;
      return evt;
    }

    // End of tag
    if (*pos_ == '>') {
      ++pos_;
      evt.attributes = attr_buf_;
      return evt;
    }

    // Attribute: name="value" or name='value'
    const char *attr_name_start = pos_;
    while (pos_ < end_ && *pos_ != '=' && *pos_ != ' ' && *pos_ != '>' && *pos_ != '/') {
      ++pos_;
    }
    std::string_view attr_name(attr_name_start, static_cast<size_t>(pos_ - attr_name_start));

    if (pos_ >= end_ || *pos_ != '=') {
      // Attribute without value (rare), skip
      continue;
    }
    ++pos_; // skip '='

    if (pos_ >= end_) {
      return std::unexpected(Error::UNEXPECTED_EOF);
    }

    char quote = *pos_;
    if (quote != '"' && quote != '\'') {
      return std::unexpected(Error::MALFORMED_TAG);
    }
    ++pos_; // skip opening quote

    const char *val_start = pos_;
    while (pos_ < end_ && *pos_ != quote) {
      ++pos_;
    }
    std::string_view attr_val(val_start, static_cast<size_t>(pos_ - val_start));
    if (pos_ < end_)
      ++pos_; // skip closing quote

    attr_buf_.push_back({attr_name, attr_val});
  }

  evt.attributes = attr_buf_;
  return evt;
}
