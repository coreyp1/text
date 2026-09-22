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

/* Every encoding has to mean the same document.
 *
 * It did not. An event's offset counts bytes of the *decoded* character
 * stream; the helpers in yaml_parser.c that ask "what stands between here and
 * the start of the line?" were reading the buffer the caller handed in. Those
 * are the same bytes only for UTF-8 with no byte order mark - which is what
 * kept it hidden, and what made it worse than it looked, because a mark in
 * front of ordinary UTF-8 shifts every offset by three and breaks it just as
 * completely as UTF-16 does. "a: 1" over "b: 2" was refused outright with
 * "Mapping key beside a node already on this line".
 *
 * yaml-test-suite has no case in any encoding but UTF-8, so `make conformance`
 * cannot see any of this. These are the gate. */

enum class Enc { Utf8, Utf8Bom, Utf16Le, Utf16Be, Utf32Le, Utf32Be };

static const char *enc_name(Enc e) {
  switch (e) {
    case Enc::Utf8:    return "UTF-8";
    case Enc::Utf8Bom: return "UTF-8 with BOM";
    case Enc::Utf16Le: return "UTF-16LE";
    case Enc::Utf16Be: return "UTF-16BE";
    case Enc::Utf32Le: return "UTF-32LE";
    case Enc::Utf32Be: return "UTF-32BE";
  }
  return "?";
}

/* ASCII in, because what is being tested is where the bytes of a character
   land rather than which character it is. */
static std::string encode(const std::string &ascii, Enc e) {
  std::string out;
  switch (e) {
    case Enc::Utf8:
      return ascii;
    case Enc::Utf8Bom:
      out.append("\xEF\xBB\xBF");
      out.append(ascii);
      return out;
    case Enc::Utf16Le: out.append("\xFF\xFE", 2); break;
    case Enc::Utf16Be: out.append("\xFE\xFF", 2); break;
    case Enc::Utf32Le: out.append("\xFF\xFE\x00\x00", 4); break;
    case Enc::Utf32Be: out.append("\x00\x00\xFE\xFF", 4); break;
  }
  const bool wide = (e == Enc::Utf32Le || e == Enc::Utf32Be);
  const bool big = (e == Enc::Utf16Be || e == Enc::Utf32Be);
  for (char ch : ascii) {
    const char zero = '\0';
    if (big) {
      if (wide) { out.push_back(zero); out.push_back(zero); }
      out.push_back(zero);
      out.push_back(ch);
    } else {
      out.push_back(ch);
      out.push_back(zero);
      if (wide) { out.push_back(zero); out.push_back(zero); }
    }
  }
  return out;
}

/* What the document says, spelled one way, so that two parses can be compared
   without walking them. Empty when it was refused. */
static std::string canonical(const std::string &bytes, bool *parsed) {
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
  GTEXT_YAML_Document *doc =
      gtext_yaml_parse(bytes.data(), bytes.size(), &popts, &err);
  gtext_yaml_error_free(&err);
  *parsed = (doc != nullptr);
  if (!doc) return std::string();

  GTEXT_YAML_Sink sink;
  std::string out;
  if (gtext_yaml_sink_buffer(&sink) == GTEXT_YAML_OK) {
    GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
    if (gtext_yaml_write_document(doc, &sink, &wopts) == GTEXT_YAML_OK) {
      const char *data = gtext_yaml_sink_buffer_data(&sink);
      out.assign(data ? data : "", gtext_yaml_sink_buffer_size(&sink));
    }
    gtext_yaml_sink_buffer_free(&sink);
  }
  gtext_yaml_free(doc);
  return out;
}

TEST(YamlEncoding, EveryEncodingMeansTheSameDocument) {
  /* Shapes chosen for the questions the positional helpers ask: is this the
     first thing on its line, what column does this entry begin at, is there
     already a node beside it, is this the "---" line. A single-entry mapping
     asks none of them, which is why the tests that were here passed. */
  static const char *documents[] = {
    "a: 1\nb: 2\n",
    "a:\n: 1\n",
    "outer:\n  x: 1\n  b: 2\n",
    "- 1\n- 2\n",
    "a:\n- 1\n- 2\n",
    "? a\n: b\n? c\n: d\n",
    "&anchor a: 1\nb: 2\n",
    "a:\n{}: 1\n",
    "a: [b, c]\nd: 2\n",
    "-: 1\nx: 2\n",
    "key: |\n  text\nnext: 1\n",
    "- a: 1\n  b: 2\n",
    /* Refused, and it has to stay refused in every encoding: a block
       collection may not begin on the "---" line. */
    "--- a: b\n",
    /* Refused for the opposite reason - a second node beside a finished one. */
    "x: { y: z }in: valid\n",
  };
  static const Enc encodings[] = {
    Enc::Utf8Bom, Enc::Utf16Le, Enc::Utf16Be, Enc::Utf32Le, Enc::Utf32Be,
  };

  for (const char *text : documents) {
    bool base_parsed = false;
    const std::string base = canonical(encode(text, Enc::Utf8), &base_parsed);
    for (Enc e : encodings) {
      bool parsed = false;
      const std::string got = canonical(encode(text, e), &parsed);
      EXPECT_EQ(parsed, base_parsed)
        << enc_name(e) << " disagrees with UTF-8 about whether <<" << text
        << ">> is a document at all";
      if (parsed && base_parsed) {
        EXPECT_EQ(got, base)
          << enc_name(e) << " read <<" << text << ">> as something else";
      }
    }
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
