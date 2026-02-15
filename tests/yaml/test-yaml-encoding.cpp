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
