/**
 * @file test-scale.cpp
 * @brief Inputs large enough to make the parsers and writers grow their
 *        internal buffers.
 *
 * Every dynamic structure in this library starts at a fixed capacity and
 * doubles. A coverage run showed that most of those growth paths were never
 * reached: the suite's inputs are all small, so the parser stacks, anchor
 * tables and writer stacks stayed at their initial size in every test.
 *
 * That is the same shape as a bug found in ctang, where a hash table grew on
 * its 33rd insertion and corrupted itself, and nothing in that suite had ever
 * inserted 33 of anything.
 *
 * Each case here sits just past one of those thresholds, and checks the
 * result rather than only that the parse returned - a growth path that
 * reallocates but drops or duplicates an entry would otherwise pass.
 */

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/csv.h>
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>
}

namespace {

/** Owns a parsed YAML document for the duration of a test. */
class YamlDoc {
public:
  explicit YamlDoc(const std::string & text) {
    memset(&error_, 0, sizeof(error_));
    doc_ = gtext_yaml_parse(text.c_str(), text.size(), nullptr, &error_);
  }
  ~YamlDoc() {
    if (doc_) {
      gtext_yaml_free(doc_);
    }
  }
  YamlDoc(const YamlDoc &) = delete;
  YamlDoc & operator=(const YamlDoc &) = delete;

  GTEXT_YAML_Document * get() const { return doc_; }
  const char * error() const {
    return error_.message ? error_.message : "unknown";
  }

private:
  GTEXT_YAML_Document * doc_ = nullptr;
  GTEXT_YAML_Error error_;
};

} // namespace

//
// YAML: anchor and alias tables start at 16 entries.
//

TEST(Scale, YamlManyAnchorsAndAliases) {
  // 40 of each, so both tables double more than once. A growth step that
  // loses an entry shows up as an alias that fails to resolve.
  const int count = 40;
  std::string yaml = "defs:\n";
  for (int i = 0; i < count; i++) {
    yaml += "  - &a" + std::to_string(i) + " value" + std::to_string(i) + "\n";
  }
  yaml += "uses:\n";
  for (int i = 0; i < count; i++) {
    yaml += "  - *a" + std::to_string(i) + "\n";
  }

  YamlDoc doc(yaml);
  ASSERT_NE(doc.get(), nullptr) << "parse failed: " << doc.error();

  const GTEXT_YAML_Node * root = gtext_yaml_document_root(doc.get());
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node * uses = gtext_yaml_mapping_get(root, "uses");
  ASSERT_NE(uses, nullptr);
  ASSERT_EQ(gtext_yaml_sequence_length(uses), (size_t)count);

  for (int i = 0; i < count; i++) {
    const GTEXT_YAML_Node * item =
        gtext_yaml_sequence_get(uses, (size_t)i);
    ASSERT_NE(item, nullptr) << "alias " << i << " missing";
    ASSERT_EQ(gtext_yaml_node_type(item), GTEXT_YAML_ALIAS) << "alias " << i;

    // The DOM keeps aliases as nodes pointing at their anchor rather than
    // expanding them, so this follows the link the anchor table produced.
    const GTEXT_YAML_Node * target = gtext_yaml_alias_target(item);
    ASSERT_NE(target, nullptr)
        << "alias " << i << " has no target: the anchor table lost it";
    const char * value = gtext_yaml_node_as_string(target);
    ASSERT_NE(value, nullptr) << "alias " << i << " target is not a scalar";
    EXPECT_STREQ(value, ("value" + std::to_string(i)).c_str())
        << "alias " << i << " points at the wrong anchor";
  }
}

//
// YAML: the parser's nesting stack starts at 32 levels.
//

TEST(Scale, YamlDeepNesting) {
  // 60 levels of block sequence, past the initial stack and one doubling.
  const int depth = 60;
  std::string yaml;
  for (int i = 0; i < depth; i++) {
    yaml += std::string((size_t)i * 2, ' ') + "- \n";
  }
  yaml += std::string((size_t)depth * 2, ' ') + "- leaf\n";

  YamlDoc doc(yaml);
  ASSERT_NE(doc.get(), nullptr) << "parse failed: " << doc.error();

  // Walk all the way down; a stack that lost a frame during growth would
  // flatten the structure somewhere along the way.
  const GTEXT_YAML_Node * node = gtext_yaml_document_root(doc.get());
  ASSERT_NE(node, nullptr);
  for (int i = 0; i < depth; i++) {
    ASSERT_EQ(gtext_yaml_node_type(node), GTEXT_YAML_SEQUENCE)
        << "level " << i << " is not a sequence";
    ASSERT_EQ(gtext_yaml_sequence_length(node), 1u) << "level " << i;
    node = gtext_yaml_sequence_get(node, 0);
    ASSERT_NE(node, nullptr) << "level " << i << " has no child";
  }
}

TEST(Scale, YamlDeepNestingRoundTrips) {
  // The writer keeps its own nesting stack, which grows separately.
  const int depth = 60;
  std::string yaml;
  for (int i = 0; i < depth; i++) {
    yaml += std::string((size_t)i * 2, ' ') + "- \n";
  }
  yaml += std::string((size_t)depth * 2, ' ') + "- leaf\n";

  YamlDoc doc(yaml);
  ASSERT_NE(doc.get(), nullptr) << "parse failed: " << doc.error();

  GTEXT_YAML_Sink sink;
  ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  ASSERT_EQ(gtext_yaml_write_document(doc.get(), &sink, &opts), GTEXT_YAML_OK);

  const char * out = gtext_yaml_sink_buffer_data(&sink);
  ASSERT_NE(out, nullptr);
  EXPECT_NE(strstr(out, "leaf"), nullptr)
      << "the deepest value did not survive the round trip";

  // And what came back out must parse to the same depth.
  std::string round_tripped(out);
  YamlDoc again(round_tripped);
  ASSERT_NE(again.get(), nullptr) << "reparse failed: " << again.error();
  const GTEXT_YAML_Node * node = gtext_yaml_document_root(again.get());
  for (int i = 0; i < depth; i++) {
    ASSERT_NE(node, nullptr) << "level " << i << " lost in the round trip";
    ASSERT_EQ(gtext_yaml_node_type(node), GTEXT_YAML_SEQUENCE)
        << "level " << i;
    node = gtext_yaml_sequence_get(node, 0);
  }

  gtext_yaml_sink_buffer_free(&sink);
}

//
// JSON: the DOM parser and the writer both keep nesting stacks.
//

TEST(Scale, JsonDeepNesting) {
  // 200 levels: past the initial stack of any of the three implementations
  // (DOM, streaming, writer) and several doublings.
  const int depth = 200;
  std::string json;
  for (int i = 0; i < depth; i++) {
    json += "[";
  }
  json += "42";
  for (int i = 0; i < depth; i++) {
    json += "]";
  }

  GTEXT_JSON_Value * root = gtext_json_parse(json.c_str(), json.size(),
      nullptr, nullptr);
  ASSERT_NE(root, nullptr) << "parse failed at depth " << depth;

  const GTEXT_JSON_Value * node = root;
  for (int i = 0; i < depth; i++) {
    ASSERT_EQ(gtext_json_typeof(node), GTEXT_JSON_ARRAY) << "level " << i;
    ASSERT_EQ(gtext_json_array_size(node), 1u) << "level " << i;
    node = gtext_json_array_get(node, 0);
    ASSERT_NE(node, nullptr) << "level " << i << " has no child";
  }
  EXPECT_EQ(gtext_json_typeof(node), GTEXT_JSON_NUMBER);

  gtext_json_free(root);
}

TEST(Scale, JsonWideArray) {
  // The array element buffer starts at 8 and doubles; 5000 elements takes it
  // through many reallocations, and every element must survive in order.
  const int count = 5000;
  std::string json = "[";
  for (int i = 0; i < count; i++) {
    if (i) {
      json += ",";
    }
    json += std::to_string(i);
  }
  json += "]";

  GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
  opts.parse_int64 = true;
  GTEXT_JSON_Value * root = gtext_json_parse(json.c_str(), json.size(),
      &opts, nullptr);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(gtext_json_typeof(root), GTEXT_JSON_ARRAY);
  ASSERT_EQ(gtext_json_array_size(root), (size_t)count);

  for (int i = 0; i < count; i++) {
    const GTEXT_JSON_Value * v = gtext_json_array_get(root, (size_t)i);
    ASSERT_NE(v, nullptr) << "element " << i << " missing";
    int64_t value = -1;
    ASSERT_EQ(gtext_json_get_i64(v, &value), GTEXT_JSON_OK) << "element " << i;
    EXPECT_EQ(value, (int64_t)i) << "element " << i << " has the wrong value";
  }

  gtext_json_free(root);
}

TEST(Scale, JsonWideObject) {
  // Object storage grows the same way, and a rehash or realloc that loses a
  // key shows up as a lookup miss rather than a crash.
  const int count = 2000;
  std::string json = "{";
  for (int i = 0; i < count; i++) {
    if (i) {
      json += ",";
    }
    json += "\"k" + std::to_string(i) + "\":" + std::to_string(i);
  }
  json += "}";

  GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
  opts.parse_int64 = true;
  GTEXT_JSON_Value * root = gtext_json_parse(json.c_str(), json.size(),
      &opts, nullptr);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(gtext_json_object_size(root), (size_t)count);

  for (int i = 0; i < count; i++) {
    std::string key = "k" + std::to_string(i);
    const GTEXT_JSON_Value * v = gtext_json_object_get(root, key.c_str(), key.size());
    ASSERT_NE(v, nullptr) << "key " << key << " lost";
    int64_t value = -1;
    ASSERT_EQ(gtext_json_get_i64(v, &value), GTEXT_JSON_OK) << "key " << key;
    EXPECT_EQ(value, (int64_t)i) << "key " << key;
  }

  gtext_json_free(root);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
