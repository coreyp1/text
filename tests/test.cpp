/**
 * @file
 *
 * Tests for the library-wide version API in src/text.c.
 *
 * This file used to contain a single EXPECT_TRUE(true) placeholder, which is
 * why src/text.c sat at 0% line coverage while every other module was
 * measured.  The accessors are trivial, but two things about them are worth
 * pinning down: that the string and the three integers cannot drift apart,
 * and that gtext_version_string() is safe to call from several threads.  It
 * once formatted into a function-local static guarded by a second static
 * flag, so two callers could both find the flag clear and both write the
 * buffer while a third read it half-written.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/text/text.h>

TEST(Version, AccessorsMatchCompileTimeMacros) {
  // A mismatch here means the loaded shared library is not the one these
  // headers describe, which is the failure this catches in an installed-prefix
  // build.
  EXPECT_EQ(gtext_version_major(), (uint32_t)GTEXT_VERSION_MAJOR);
  EXPECT_EQ(gtext_version_minor(), (uint32_t)GTEXT_VERSION_MINOR);
  EXPECT_EQ(gtext_version_patch(), (uint32_t)GTEXT_VERSION_PATCH);
}

TEST(Version, StringIsWellFormed) {
  const char * s = gtext_version_string();
  ASSERT_NE(s, nullptr);
  EXPECT_GT(std::strlen(s), 0u);

  unsigned major = 0, minor = 0, patch = 0;
  // "%u.%u.%u" stops at the first character that is not part of the third
  // number, so a "-dev" or "-rc1" suffix still parses.
  ASSERT_EQ(std::sscanf(s, "%u.%u.%u", &major, &minor, &patch), 3)
      << "version string \"" << s << "\" is not major.minor.patch";
}

TEST(Version, StringAgreesWithIntegers) {
  // The string and the integers come from the same generated header.  If
  // anyone reintroduces run-time formatting, or hand-writes the string, this
  // is where the two spellings diverge.
  const char * s = gtext_version_string();
  ASSERT_NE(s, nullptr);

  char expected[64];
  std::snprintf(expected, sizeof(expected), "%u.%u.%u",
      (unsigned)gtext_version_major(), (unsigned)gtext_version_minor(),
      (unsigned)gtext_version_patch());

  // Prefix rather than equality, so a pre-release suffix is allowed.
  EXPECT_EQ(std::strncmp(s, expected, std::strlen(expected)), 0)
      << "string \"" << s << "\" does not begin with \"" << expected << "\"";
}

TEST(Version, StringIsAStableLiteral) {
  // Two calls returning the same pointer is the observable consequence of the
  // value being known at compile time.  A future implementation that formats
  // into a per-call or function-local buffer fails here, before it has a
  // chance to fail intermittently under threads.
  const char * a = gtext_version_string();
  const char * b = gtext_version_string();
  EXPECT_EQ(a, b);
}

TEST(Version, StringIsThreadSafe) {
  // The regression test for the data race described in src/text.c.  Under the
  // old implementation this is the shape that produced a partially written
  // buffer; proving the race itself needs ThreadSanitizer, but a torn string
  // is caught here by content alone.
  //
  // Each thread records the FIRST value it sees that differs from the
  // expected one and never overwrites it.  Keeping only the latest observed
  // value would let a transient tear be erased by the next good read, which
  // is the difference between a test that can fail and one that only looks
  // like it can.
  const std::string expected = gtext_version_string();

  const int thread_count = 16;
  const int iterations = 2000;
  std::vector<std::thread> threads;
  std::vector<std::string> first_bad(thread_count);

  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([i, iterations, &expected, &first_bad]() {
      for (int j = 0; j < iterations; ++j) {
        const char * s = gtext_version_string();
        if (!first_bad[i].empty()) {
          continue;
        }
        if (!s) {
          first_bad[i] = "<null>";
        }
        else if (expected != s) {
          first_bad[i] = s;
        }
      }
    });
  }
  for (auto & t : threads) {
    t.join();
  }

  for (int i = 0; i < thread_count; ++i) {
    EXPECT_TRUE(first_bad[i].empty())
        << "thread " << i << " saw \"" << first_bad[i] << "\" instead of \""
        << expected << "\"";
  }
}

TEST(Version, PackedVersionMatchesComponents) {
  EXPECT_EQ((unsigned)GTEXT_VERSION_NUMBER,
      GTEXT_MAKE_VERSION(gtext_version_major(), gtext_version_minor(),
          gtext_version_patch()));
}

TEST(Version, PackedVersionOrdersCorrectly) {
  // The point of the packed form is that `<` is a correct version comparison,
  // which only holds if each component occupies its own byte.
  EXPECT_LT(GTEXT_MAKE_VERSION(1, 2, 3), GTEXT_MAKE_VERSION(1, 2, 4));
  EXPECT_LT(GTEXT_MAKE_VERSION(1, 2, 255), GTEXT_MAKE_VERSION(1, 3, 0));
  EXPECT_LT(GTEXT_MAKE_VERSION(1, 255, 255), GTEXT_MAKE_VERSION(2, 0, 0));
  EXPECT_EQ(GTEXT_MAKE_VERSION(1, 2, 3), 0x010203u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
