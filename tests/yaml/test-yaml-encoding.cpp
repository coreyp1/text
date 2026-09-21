/**
 * @file test-yaml-encoding.cpp
 * @brief Tests for BOM detection and UTF-16/32 decoding.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static std::vector<unsigned char> utf16_with_bom(
    const char *ascii,
    bool big_endian) {
  std::vector<unsigned char> out;
  if (big_endian) {
    out.push_back(0xFE);
    out.push_back(0xFF);
  } else {
    out.push_back(0xFF);
    out.push_back(0xFE);
  }

  for (const char *p = ascii; *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (big_endian) {
      out.push_back(0x00);
      out.push_back(c);
    } else {
      out.push_back(c);
      out.push_back(0x00);
    }
  }
  return out;
}

static std::vector<unsigned char> utf32_with_bom(
    const char *ascii,
    bool big_endian) {
  std::vector<unsigned char> out;
  if (big_endian) {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0xFE);
    out.push_back(0xFF);
  } else {
    out.push_back(0xFF);
    out.push_back(0xFE);
    out.push_back(0x00);
    out.push_back(0x00);
  }

  for (const char *p = ascii; *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (big_endian) {
      out.push_back(0x00);
      out.push_back(0x00);
      out.push_back(0x00);
      out.push_back(c);
    } else {
      out.push_back(c);
      out.push_back(0x00);
      out.push_back(0x00);
      out.push_back(0x00);
    }
  }
  return out;
}

static void expect_mapping_value(
    const char *data,
    size_t len,
    const char *key,
    const char *value) {
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(data, len, nullptr, &err);
  ASSERT_NE(doc, nullptr) << "Parse failed: "
      << (err.message ? err.message : "unknown");

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *node = gtext_yaml_mapping_get(root, key);
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(node), value);

  gtext_yaml_free(doc);
}

TEST(YamlEncoding, Utf8Bom) {
  const char *payload = "key: value";
  std::string input;
  input.push_back(static_cast<char>(0xEF));
  input.push_back(static_cast<char>(0xBB));
  input.push_back(static_cast<char>(0xBF));
  input.append(payload);

  expect_mapping_value(input.data(), input.size(), "key", "value");
}

TEST(YamlEncoding, Utf16LeBom) {
  auto bytes = utf16_with_bom("key: value", false);
  expect_mapping_value(reinterpret_cast<const char *>(bytes.data()),
      bytes.size(), "key", "value");
}

TEST(YamlEncoding, Utf16BeBom) {
  auto bytes = utf16_with_bom("key: value", true);
  expect_mapping_value(reinterpret_cast<const char *>(bytes.data()),
      bytes.size(), "key", "value");
}

TEST(YamlEncoding, Utf32LeBom) {
  auto bytes = utf32_with_bom("key: value", false);
  expect_mapping_value(reinterpret_cast<const char *>(bytes.data()),
      bytes.size(), "key", "value");
}

TEST(YamlEncoding, Utf32BeBom) {
  auto bytes = utf32_with_bom("key: value", true);
  expect_mapping_value(reinterpret_cast<const char *>(bytes.data()),
      bytes.size(), "key", "value");
}

TEST(YamlEncoding, WriterRoundTripUtf16Le) {
  const char *input = "key: value";
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
  ASSERT_NE(doc, nullptr) << "Parse failed: "
      << (err.message ? err.message : "unknown");

  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.encoding = GTEXT_YAML_ENCODING_UTF16LE;
  opts.emit_bom = true;

  status = gtext_yaml_write_document(doc, &sink, &opts);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  const char *output = gtext_yaml_sink_buffer_data(&sink);
  size_t output_len = gtext_yaml_sink_buffer_size(&sink);
  ASSERT_NE(output, nullptr);

  expect_mapping_value(output, output_len, "key", "value");

  gtext_yaml_sink_buffer_free(&sink);
  gtext_yaml_free(doc);
}

/* Event offsets index the *decoded* character stream; the parser's positional
   helpers index the raw input buffer the caller handed in. For UTF-8 those
   are the same bytes and nobody noticed. For UTF-16 they are not, and every
   question of the form "what stands between here and the start of the line?"
   is answered from the wrong place - so a block mapping with two entries does
   not parse at all.

   One of those helpers also had no bound check, so the mismatch was a
   heap-buffer-overflow: it scans *backwards* from the offset, and an offset
   past the end of the buffer made the first read land off the allocation.
   ASan called it 22 bytes before whatever the allocator had put next. The
   clamp is in; the offsets are not, and are recorded on the YAML format page
   under Known defects.

   This test says what is wrong rather than what is right, and is written to
   keep passing when it is fixed. */
TEST(YamlEncoding, Utf16BlockMappingsDoNotParseYet) {
  const char *documents[] = {
    "a: 1\nb: 2\n",
    "a:\n: 1\n",
    "outer:\n  x: 1\n  b: 2\n",
  };
  for (const char *utf8 : documents) {
    const std::string in8(utf8);
    std::string in16;
    in16.push_back('\xff');
    in16.push_back('\xfe');
    for (char ch : in8) { in16.push_back(ch); in16.push_back('\0'); }

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Document *a =
      gtext_yaml_parse(in8.data(), in8.size(), &opts, &err);
    ASSERT_NE(a, nullptr) << "utf-8: " << (err.message ? err.message : "");
    gtext_yaml_error_free(&err);
    gtext_yaml_free(a);

    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *b =
      gtext_yaml_parse(in16.data(), in16.size(), &opts, &err);
    if (b) {
      /* It got fixed. Good - delete this test's excuse and keep the assert. */
      gtext_yaml_free(b);
      gtext_yaml_error_free(&err);
      continue;
    }
    EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID)
      << "utf-16 of <<" << utf8 << ">>: " << (err.message ? err.message : "");
    gtext_yaml_error_free(&err);
  }
}

/* What the clamp guarantees regardless: no read outside the buffer. Only
   meaningful under ASan, which is why `make test-asan` is a separate run. */
TEST(YamlEncoding, Utf16DoesNotReadOutsideTheInputBuffer) {
  static const unsigned char bytes[] = {
    0xff, 0xfe, 0x22, 0x7c, 0x0a, 0x2a, 0x2d, 0x2d, 0x2d, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x5d, 0x2d, 0x2d, 0x2d, 0x0a, 0x7c, 0x7c, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x00, 0x74, 0xc4, 0x65, 0x00, 0x0d, 0x09, 0x3e, 0x0d,
    0x0d, 0x0a, 0x2d, 0x2d, 0x0a, 0x0a, 0x0a, 0x0a, 0x5d, 0x2d, 0x2d, 0x2d,
    0x0a, 0x2d, 0x2d, 0x0a, 0x50, 0x00, 0x02, 0x0d, 0x3a, 0x00,
  };
  const std::string in(reinterpret_cast<const char *>(bytes), sizeof(bytes));
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  GTEXT_YAML_Document *doc =
    gtext_yaml_parse(in.data(), in.size(), &opts, &err);
  if (doc) gtext_yaml_free(doc);
  gtext_yaml_error_free(&err);
  SUCCEED();  /* reaching here with no sanitizer report is the assertion */
}
