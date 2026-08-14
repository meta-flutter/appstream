// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 Joel Winarske <joel.winarske@gmail.com>

#include <gtest/gtest.h>

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "AppStreamParser.h"
#include "Component.h"
#include "SqliteWriter.h"
#include "StringPool.h"
#include "XmlScanner.h"

namespace fs = std::filesystem;

// ============================================================
// Benchmark utilities
// ============================================================

class BenchmarkTimer {
  using Clock = std::chrono::high_resolution_clock;
  using Duration = std::chrono::duration<double, std::milli>;

  Clock::time_point start_;
  std::string name_;

public:
  explicit BenchmarkTimer(const std::string &name) : start_(Clock::now()), name_(name) {}

  ~BenchmarkTimer() {
    const auto elapsed = Duration(Clock::now() - start_);
    std::cout << "⏱️  " << name_ << ": " << elapsed.count() << " ms" << std::endl;
  }
};

// ============================================================
// Performance benchmarks
// ============================================================

class PerformanceBenchmark : public ::testing::Test {
protected:
  static constexpr size_t COMPONENT_COUNT = 1000;
  static constexpr size_t LARGE_COMPONENT_COUNT = 5000;

  static std::string generateXML(const size_t count, const size_t categories_per_component = 5) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<components version=\"0.14\">\n";

    for (size_t i = 0; i < count; ++i) {
      ss << "  <component type=\"desktop-application\">\n";
      ss << "    <id>app.example.component" << i << "</id>\n";
      ss << "    <name>Component " << i << "</name>\n";
      ss << "    <summary>Test component number " << i << "</summary>\n";
      ss << "    <description>\n";
      ss << "      <p>This is a test component for benchmarking.</p>\n";
      ss << "      <p>Component ID: " << i << "</p>\n";
      ss << "    </description>\n";

      for (size_t j = 0; j < categories_per_component; ++j) {
        ss << "    <categories>\n";
        ss << "      <category>Category" << j << "</category>\n";
        ss << "    </categories>\n";
      }

      ss << "    <url type=\"homepage\">https://example.com/" << i << "</url>\n";
      ss << "  </component>\n";
    }

    ss << "</components>\n";
    return ss.str();
  }

  static std::string createTempFile(const std::string &content) {
    static int counter = 0;
    auto path = "/tmp/appstream_bench_" + std::to_string(++counter) + ".xml";
    std::ofstream file(path);
    file << content;
    file.close();
    return path;
  }

  static std::string createTempDB() {
    static int counter = 0;
    return "/tmp/appstream_bench_" + std::to_string(++counter) + ".db";
  }

  static void cleanupFile(const std::string &path) {
    if (fs::exists(path)) {
      fs::remove(path);
    }
  }
};

// ============================================================
// StringPool benchmark
// ============================================================

TEST_F(PerformanceBenchmark, StringPoolInterning) {
  {
    BenchmarkTimer timer("StringPool: 10k string interner");
    StringPool pool;

    for (size_t i = 0; i < 10000; ++i) {
      std::string str = "string_" + std::to_string(i % 100);
      (void)pool.intern(str);
    }

    EXPECT_GT(pool.size(), 0u);
  }
}

// ============================================================
// XmlScanner benchmark
// ============================================================

TEST_F(PerformanceBenchmark, XmlScannerLargeDocument) {
  auto xml_path = createTempFile(generateXML(COMPONENT_COUNT));

  {
    BenchmarkTimer timer("XmlScanner: Parse " + std::to_string(COMPONENT_COUNT) + " components");

    int fd = open(xml_path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0) << "Failed to open " << xml_path;
    XmlScanner scanner(fd);

    size_t event_count = 0;
    while (true) {
      auto result = scanner.next();
      if (!result.has_value())
        break;
      if (result->type == XmlScanner::EventType::END_OF_DOCUMENT)
        break;
      event_count++;
    }
    close(fd);

    EXPECT_GT(event_count, 0u);
  }

  cleanupFile(xml_path);
}

// ============================================================
// AppStreamParser benchmark
// ============================================================

class MockSink : public ComponentSink {
  size_t count_ = 0;

public:
  std::expected<void, Error> begin() override { return {}; }

  std::expected<void, Error> onComponent(Component /*component*/) override {
    count_++;
    return {};
  }

  std::expected<void, Error> end() override { return {}; }

  [[nodiscard]] size_t componentCount() const override { return count_; }
};

TEST_F(PerformanceBenchmark, AppStreamParserStreamingMode) {
  auto xml_path = createTempFile(generateXML(COMPONENT_COUNT));

  {
    BenchmarkTimer timer("AppStreamParser (streaming): Parse " + std::to_string(COMPONENT_COUNT) +
                         " components");

    MockSink sink;
    auto result = AppStreamParser::parseToSink(xml_path, "en", sink);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(sink.componentCount(), COMPONENT_COUNT);
  }

  cleanupFile(xml_path);
}

TEST_F(PerformanceBenchmark, AppStreamParserInMemoryMode) {
  auto xml_path = createTempFile(generateXML(COMPONENT_COUNT / 10));

  {
    BenchmarkTimer timer("AppStreamParser (in-memory): Parse and search");

    auto result = AppStreamParser::create(xml_path, "en");

    EXPECT_TRUE(result.has_value());
    EXPECT_GT(result->getTotalComponentCount(), 0u);

    // Simulate search operations
    for (size_t i = 0; i < 100; ++i) {
      auto filtered = result->searchByCategory("Category" + std::to_string(i % 5));
      (void)filtered;
    }
  }

  cleanupFile(xml_path);
}

// ============================================================
// SqliteWriter benchmark
// ============================================================

TEST_F(PerformanceBenchmark, SqliteWriterBatchCommit) {
  auto db_path = createTempDB();
  auto xml_path = createTempFile(generateXML(COMPONENT_COUNT));

  {
    BenchmarkTimer timer("SqliteWriter: Write " + std::to_string(COMPONENT_COUNT) +
                         " components to SQLite");

    {
      SqliteWriter writer(db_path);
      auto result = AppStreamParser::parseToSink(xml_path, "en", writer);
      EXPECT_TRUE(result.has_value());
    }

    // Verify the database was created
    EXPECT_TRUE(fs::exists(db_path));
  }

  cleanupFile(xml_path);
  cleanupFile(db_path);
}

// ============================================================
// End-to-end benchmark
// ============================================================

TEST_F(PerformanceBenchmark, EndToEndParsing) {
  auto xml_path = createTempFile(generateXML(COMPONENT_COUNT));
  auto db_path = createTempDB();

  {
    BenchmarkTimer timer("End-to-end: Parse XML to SQLite (" + std::to_string(COMPONENT_COUNT) +
                         " components)");

    {
      SqliteWriter writer(db_path);
      auto result = AppStreamParser::parseToSink(xml_path, "en", writer);
      EXPECT_TRUE(result.has_value());
    }

    // Verify result
    EXPECT_TRUE(fs::exists(db_path));
    auto file_size = fs::file_size(db_path);
    EXPECT_GT(file_size, static_cast<uintmax_t>(1024)); // At least 1KB
    std::cout << "   Database size: " << file_size << " bytes" << std::endl;
  }

  cleanupFile(xml_path);
  cleanupFile(db_path);
}

// ============================================================
// Stress test: Large dataset
// ============================================================

TEST_F(PerformanceBenchmark, StressTestLargeDataset) {
  auto xml_path = createTempFile(generateXML(LARGE_COMPONENT_COUNT));
  auto db_path = createTempDB();

  {
    BenchmarkTimer timer("Stress test: " + std::to_string(LARGE_COMPONENT_COUNT) + " components");

    {
      SqliteWriter writer(db_path);
      auto result = AppStreamParser::parseToSink(xml_path, "en", writer);
      EXPECT_TRUE(result.has_value());
      EXPECT_EQ(writer.componentCount(), LARGE_COMPONENT_COUNT);
    }

    auto file_size = fs::file_size(db_path);
    std::cout << "   Final database size: " << file_size << " bytes" << std::endl;
    std::cout << "   Avg bytes/component: " << (file_size / LARGE_COMPONENT_COUNT) << std::endl;
  }

  cleanupFile(xml_path);
  cleanupFile(db_path);
}

// ============================================================
// Memory efficiency benchmark
// ============================================================

TEST_F(PerformanceBenchmark, StringPoolMemoryEfficiency) {
  {
    BenchmarkTimer timer("StringPool: Memory deduplication");

    StringPool pool;
    static constexpr size_t ITERATIONS = 10000;
    static constexpr size_t UNIQUE_STRINGS = 100;

    for (size_t i = 0; i < ITERATIONS; ++i) {
      std::string str = "duplicate_string_" + std::to_string(i % UNIQUE_STRINGS);
      (void)pool.intern(str);
    }

    // Verify deduplication: should only have ~UNIQUE_STRINGS unique strings
    EXPECT_LE(pool.size(), UNIQUE_STRINGS + 10);
    std::cout << "   Interned " << ITERATIONS << " strings into " << pool.size()
              << " unique entries" << std::endl;
  }
}

// ============================================================
// Component creation benchmark
// ============================================================

TEST_F(PerformanceBenchmark, ComponentCreationAndConversion) {
  {
    BenchmarkTimer timer("Component: Create and convert 10k components");

    for (size_t i = 0; i < 10000; ++i) {
      Component comp;
      comp.id = "app.example.test" + std::to_string(i);
      comp.name = "Test Component " + std::to_string(i);
      comp.type = Component::Type::DESKTOP_APPLICATION;
      comp.categories.push_back("Category1");
      comp.categories.push_back("Category2");

      // Simulate conversion
      auto type_str = Component::componentTypeToString(comp.type);
      (void)type_str;
    }
  }
}