/**
 * @file test-yaml-explicit-keys.cpp
 * @brief Tests for explicit key indicator handling
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlExplicitKeys, ExplicitScalarKey) {
	const char *yaml = "? foo\n: bar\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_STRING);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(key), "foo");
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "bar");

	gtext_yaml_free(doc);
}

TEST(YamlExplicitKeys, ExplicitSequenceKey) {
	const char *yaml = "? - a\n  - b\n: value\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "value");

	size_t count = gtext_yaml_sequence_length(key);
	ASSERT_EQ(count, 2u);
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(key, 0);
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(key, 1);
	ASSERT_NE(item0, nullptr);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "a");
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "b");

	gtext_yaml_free(doc);
}

TEST(YamlExplicitKeys, ExplicitMappingKey) {
	const char *yaml = "? {a: 1, b: 2}\n: ok\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "ok");

	const GTEXT_YAML_Node *key_a = gtext_yaml_mapping_get(key, "a");
	const GTEXT_YAML_Node *key_b = gtext_yaml_mapping_get(key, "b");
	ASSERT_NE(key_a, nullptr);
	ASSERT_NE(key_b, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key_a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(key_b), "2");

	gtext_yaml_free(doc);
}

// ---------------------------------------------------------------------------
// A mapping key with no value
//
// A mapping's children are collected as a flat alternating key, value, key,
// value list, and the pairs are formed by halving it.  A key whose value was
// absent left that list one short, so the halving dropped the last key and
// paired every later key with the wrong value:
//
//     a:          gave {}             instead of {a: null}
//     a:          gave {a: b}, with   instead of {a: null, b: 1}
//     b: 1          the 1 discarded
//     ? a         gave {}             instead of {a: null}
//     ? a         was a parse error   instead of {a: null, b: null}
//     ? b
//
// The second row is the one that matters: a missing value did not fail, it
// silently shifted the rest of the mapping by one.  A key with an empty value
// is ordinary in configuration files.
//
// YAML says an absent value is null.  The expectations below were taken from
// PyYAML on the same input.
// ---------------------------------------------------------------------------

namespace {

GTEXT_YAML_Document *parse_ok(const char *src, GTEXT_YAML_Error *err) {
	GTEXT_YAML_Document *d = gtext_yaml_parse(src, strlen(src), NULL, err);
	EXPECT_NE(d, nullptr) << src << " -> " << (err->message ? err->message : "");
	return d;
}

// The value at `key`, or nullptr.
const GTEXT_YAML_Node *value_of(const GTEXT_YAML_Document *doc, const char *key) {
	return gtext_yaml_mapping_get(gtext_yaml_document_root(doc), key);
}

void expect_null_value(const GTEXT_YAML_Document *doc, const char *key) {
	const GTEXT_YAML_Node *v = value_of(doc, key);
	ASSERT_NE(v, nullptr) << "key \"" << key << "\" is missing entirely";
	EXPECT_EQ(gtext_yaml_node_type(v), GTEXT_YAML_NULL)
	    << "key \"" << key << "\" has a value but it is not null";
}

} // namespace

TEST(YamlMissingValue, ImplicitKeyWithNoValueIsNull) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("a:\n", &err);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 1u)
	    << "the key was dropped rather than given a null value";
	expect_null_value(doc, "a");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlMissingValue, AMissingValueDoesNotShiftTheRestOfTheMapping) {
	// The corruption case.  "b" used to become the value of "a", and 1 was
	// discarded entirely.
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("a:\nb: 1\n", &err);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_EQ(gtext_yaml_mapping_size(root), 2u);
	expect_null_value(doc, "a");

	const GTEXT_YAML_Node *b = value_of(doc, "b");
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(b), GTEXT_YAML_INT);
	int64_t n = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(b, &n));
	EXPECT_EQ(n, 1);

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlMissingValue, SeveralMissingValuesInARow) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("a:\nb:\nc: 3\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 3u);
	expect_null_value(doc, "a");
	expect_null_value(doc, "b");
	const GTEXT_YAML_Node *c = value_of(doc, "c");
	ASSERT_NE(c, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(c), GTEXT_YAML_INT);
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlMissingValue, AnEmptyStringIsNotNull) {
	// The distinction the fix must not erase.  "a:" is null; "a: ''" is the
	// empty string.  An empty scalar would have collapsed the two, because the
	// resolver reads a scalar's text without knowing whether it was quoted.
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("a: ''\nb:\n", &err);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *a = value_of(doc, "a");
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(a), GTEXT_YAML_STRING);
	expect_null_value(doc, "b");

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlMissingValue, IndentedContentIsStillTheValue) {
	// Indentation decides.  Content indented past the key belongs to the key;
	// only a sibling at the key's own column means the value was absent.  A
	// rule that fired on "missing" alone would break both of these.
	{
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_ok("x:\n  y: 1\n", &err);
		ASSERT_NE(doc, nullptr);
		const GTEXT_YAML_Node *x = value_of(doc, "x");
		ASSERT_NE(x, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(x), GTEXT_YAML_MAPPING);
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
	{
		// A block sequence may sit at its key's own column and still be that
		// key's value, which is why the rule looks only at scalars.
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_ok("a:\n- 1\n", &err);
		ASSERT_NE(doc, nullptr);
		const GTEXT_YAML_Node *a = value_of(doc, "a");
		ASSERT_NE(a, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(a), GTEXT_YAML_SEQUENCE);
		EXPECT_EQ(gtext_yaml_sequence_length(a), 1u);
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
}

TEST(YamlExplicitKeys, ExplicitKeyWithNoValueIsNull) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\n", &err);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 1u)
	    << "the explicit key was dropped";
	expect_null_value(doc, "a");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlExplicitKeys, SeveralExplicitKeysWithNoValues) {
	// This was "Explicit key already pending", which made the block spelling
	// of a !!set unparseable.
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\n? b\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u);
	expect_null_value(doc, "a");
	expect_null_value(doc, "b");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlExplicitKeys, BlockSetParses) {
	// The shape that first exposed this: !!set in block style.  Its tag
	// survives too, which is a separate fix in the same area.
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("!!set\n? a\n? b\n", &err);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SET);
	const char *tag = gtext_yaml_node_tag(root);
	ASSERT_NE(tag, nullptr);
	EXPECT_STREQ(tag, "!!set");

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlExplicitKeys, MixedExplicitAndImplicitKeys) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\n: 1\nb: 2\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u);

	const GTEXT_YAML_Node *a = value_of(doc, "a");
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(a), GTEXT_YAML_INT);
	const GTEXT_YAML_Node *b = value_of(doc, "b");
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(b), GTEXT_YAML_INT);

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlMissingValue, FlowMappingKeyWithNoValue) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("{a: 1, b}\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u);
	const GTEXT_YAML_Node *a = value_of(doc, "a");
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(a), GTEXT_YAML_INT);
	expect_null_value(doc, "b");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

// ---------------------------------------------------------------------------
// An implicit key ends the explicit key above it
//
// An explicit key's value colon stands on a line of its own:
// c-l-block-map-explicit-value(n) is s-indent(n) ":" s-l+block-indented(n).
// A ":" with a scalar in front of it on the same line belongs to that
// scalar, so the explicit key above it simply never got a value.
//
// That was not distinguished, so the ":" of "c:" was taken for the value
// colon of the "? b" above it and refused for being at the wrong column:
//
//     ? a
//     ? b
//     c:          was "Explicit key ':' indentation mismatch"
//
// A flow mapping has no such rule - "{? foo: bar}" puts the colon on the
// same line by design - so the check only applies in block context.
// Expectations are PyYAML's.
// ---------------------------------------------------------------------------

TEST(YamlExplicitKeys, AnImplicitKeyEndsTheExplicitKeyAboveIt) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\n? b\nc:\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 3u);
	expect_null_value(doc, "a");
	expect_null_value(doc, "b");
	expect_null_value(doc, "c");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlExplicitKeys, AnImplicitKeyMayFollowAnExplicitOneDirectly) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\nb: 1\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u);
	expect_null_value(doc, "a");
	const GTEXT_YAML_Node *b = value_of(doc, "b");
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(b), GTEXT_YAML_INT);
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlExplicitKeys, AFlowExplicitKeyKeepsItsColonOnTheSameLine) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("{? foo: bar}\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 1u);
	const GTEXT_YAML_Node *foo = value_of(doc, "foo");
	ASSERT_NE(foo, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(foo), "bar");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

// An explicit key with no ":" after it is a key with a null value, which is
// the block spelling of a set. Without that, the trailing-key rule in
// finalize would read the last one as a scalar nothing had claimed.
TEST(YamlExplicitKeys, ATrailingExplicitKeyNeedsNoColon) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_ok("? a\n? b\n", &err);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u);
	expect_null_value(doc, "a");
	expect_null_value(doc, "b");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}
