/**
 * @file test-yaml-plain-scalars.cpp
 * @brief Tests for plain scalar parsing with spaces in block/flow contexts
 */

#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

/**
 * Test that plain scalars with spaces are parsed as single values in block context
 */
TEST(YamlPlainScalars, BlockContextMultiWord) {
	const char *yaml = "key: just a string\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	
	const char *str = gtext_yaml_node_as_string(value);
	ASSERT_NE(str, nullptr);
	
	// This is the critical test: the value should be "just a string", not "just"
	EXPECT_STREQ(str, "just a string");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar with multiple spaces
 */
TEST(YamlPlainScalars, BlockContextMultipleSpaces) {
	const char *yaml = "description: This is a longer description with many words\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "description");
	ASSERT_NE(value, nullptr);
	
	const char *str = gtext_yaml_node_as_string(value);
	EXPECT_STREQ(str, "This is a longer description with many words");
	
	gtext_yaml_free(doc);
}

/**
 * Test that plain scalars in flow context still work correctly (space-separated)
 */
TEST(YamlPlainScalars, FlowContextSeparation) {
	const char *yaml = "[one, two, three]\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	size_t count = gtext_yaml_sequence_length(root);
	EXPECT_EQ(count, 3u);
	
	// In flow context, these should be separate values
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "one");
	
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "two");
	
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 2);
	ASSERT_NE(item2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item2), "three");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar that contains a colon (not followed by space)
 */
TEST(YamlPlainScalars, ContainsColon) {
	const char *yaml = "url: http://example.com\ntime: 12:30:45\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *url_val = gtext_yaml_mapping_get(root, "url");
	ASSERT_NE(url_val, nullptr);
	const char *url_str = gtext_yaml_node_as_string(url_val);
	EXPECT_STREQ(url_str, "http://example.com");
	
	const GTEXT_YAML_Node *time_val = gtext_yaml_mapping_get(root, "time");
	ASSERT_NE(time_val, nullptr);
	const char *time_str = gtext_yaml_node_as_string(time_val);
	EXPECT_STREQ(time_str, "12:30:45");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar in sequence context
 */
TEST(YamlPlainScalars, InSequence) {
	const char *yaml = "- first item\n- second item\n- third item\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "first item");
	
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "second item");
	
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 2);
	ASSERT_NE(item2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item2), "third item");
	
	gtext_yaml_free(doc);
}

/**
 * Test mix of block and flow contexts
 */
TEST(YamlPlainScalars, MixedContext) {
	const char *yaml = "data:\n  items: [one, two, three]\n  note: this is a note\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

/*
 * Indicator characters inside a block-context plain scalar.
 *
 * YAML 1.2.2 restricts '-', '?', ',', '[', ']', '{', '}', '&', '*', '!', '|',
 * '>' and '%' to the position where a node may begin (c-indicator, 5.3);
 * ns-plain-char (7.3.3) makes them ordinary content once the scalar has
 * started.  ',' and the brackets are indicators only in flow context.
 *
 * The scanner used to end the scalar at a space followed by any of them, so
 * "a - b c" silently became "a".  Each expectation below was cross-checked
 * against PyYAML.
 */
namespace {

/** Parse `src` and return the scalar at the top-level key "key". */
static std::string plain_value(const char *src) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), nullptr, &err);
	EXPECT_NE(doc, nullptr) << "parse failed for " << src << ": "
	                        << (err.message ? err.message : "unknown");
	if (!doc) {
		gtext_yaml_error_free(&err);
		return "<parse failed>";
	}
	const GTEXT_YAML_Node *value =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "key");
	EXPECT_NE(value, nullptr) << "no key in " << src;
	const char *s = value ? gtext_yaml_node_as_string(value) : nullptr;
	std::string out = s ? s : "<null>";
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
	return out;
}

} // namespace

TEST(YamlPlainScalarIndicators, DashIsContentNotATerminator) {
	EXPECT_EQ(plain_value("key: a - b c\n"), "a - b c");
	EXPECT_EQ(plain_value("key: a -b c\n"), "a -b c");
	EXPECT_EQ(plain_value("key: a b - c\n"), "a b - c");
	EXPECT_EQ(plain_value("key: 3 - 4\n"), "3 - 4");
	EXPECT_EQ(plain_value("key: 1 - 2 - 3\n"), "1 - 2 - 3");
	EXPECT_EQ(plain_value("key: a  -  b\n"), "a  -  b");
}

TEST(YamlPlainScalarIndicators, TrailingDashIsKept) {
	EXPECT_EQ(plain_value("key: end-\n"), "end-");
	EXPECT_EQ(plain_value("key: a-\n"), "a-");
	EXPECT_EQ(plain_value("key: x-y-z\n"), "x-y-z");
}

TEST(YamlPlainScalarIndicators, FlowIndicatorsAreContentInBlockContext) {
	EXPECT_EQ(plain_value("key: a, b\n"), "a, b");
	EXPECT_EQ(plain_value("key: a,b,c\n"), "a,b,c");
	EXPECT_EQ(plain_value("key: foo bar, baz qux\n"), "foo bar, baz qux");
	EXPECT_EQ(plain_value("key: a [ b\n"), "a [ b");
	EXPECT_EQ(plain_value("key: a ] b\n"), "a ] b");
	EXPECT_EQ(plain_value("key: a { b\n"), "a { b");
	EXPECT_EQ(plain_value("key: a } b\n"), "a } b");
}

TEST(YamlPlainScalarIndicators, NodeIndicatorsAreContentMidScalar) {
	EXPECT_EQ(plain_value("key: a ? b\n"), "a ? b");
	EXPECT_EQ(plain_value("key: a ?b\n"), "a ?b");
	EXPECT_EQ(plain_value("key: a & b\n"), "a & b");
	EXPECT_EQ(plain_value("key: a * b\n"), "a * b");
	EXPECT_EQ(plain_value("key: a *b\n"), "a *b");
	EXPECT_EQ(plain_value("key: a ! b\n"), "a ! b");
	EXPECT_EQ(plain_value("key: a | b\n"), "a | b");
	EXPECT_EQ(plain_value("key: a > b\n"), "a > b");
	EXPECT_EQ(plain_value("key: a % b\n"), "a % b");
}

TEST(YamlPlainScalarIndicators, HashEndsTheScalarOnlyAfterSpace) {
	/* " #" opens a comment; a '#' with no space before it is content. */
	EXPECT_EQ(plain_value("key: a # b\n"), "a");
	EXPECT_EQ(plain_value("key: a#b\n"), "a#b");
}

TEST(YamlPlainScalarIndicators, ColonEndsTheScalarOnlyBeforeSpace) {
	/* ": " is the value indicator; ':' followed by anything else is content. */
	EXPECT_EQ(plain_value("key: a:b\n"), "a:b");
	EXPECT_EQ(plain_value("key: a :b c\n"), "a :b c");
	EXPECT_EQ(plain_value("key: http://x.com/y\n"), "http://x.com/y");
	/* YAML 1.2 dropped 1.1's sexagesimals, so this stays a string. */
	EXPECT_EQ(plain_value("key: 12:00\n"), "12:00");
}

TEST(YamlPlainScalarIndicators, QuotingStillWins) {
	EXPECT_EQ(plain_value("key: 'a - b'\n"), "a - b");
	EXPECT_EQ(plain_value("key: \"a - b\"\n"), "a - b");
	EXPECT_EQ(plain_value("key: !!str a - b\n"), "a - b");
}

TEST(YamlPlainScalarIndicators, FlowContextStillDelimitsOnFlowIndicators) {
	/* ',' and the brackets must still terminate a scalar inside a flow
	   collection, or it could never be closed - while '-' and '#' must not,
	   which is what used to make "[a-b, c]" fail to parse at all. */
	struct Case {
		const char *yaml;
		size_t count;
		const char *first;
	};
	const Case cases[] = {
	    {"key: [a, b]\n", 2, "a"},
	    {"key: [a-b, c]\n", 2, "a-b"},
	    {"key: [a#b, c]\n", 2, "a#b"},
	    {"key: [x-y-z]\n", 1, "x-y-z"},
	    {"key: [1, 2, 3]\n", 3, "1"},
	};

	for (const Case &c : cases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
		    gtext_yaml_parse(c.yaml, strlen(c.yaml), nullptr, &err);
		ASSERT_NE(doc, nullptr)
		    << c.yaml << ": " << (err.message ? err.message : "unknown");

		const GTEXT_YAML_Node *seq =
		    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "key");
		ASSERT_NE(seq, nullptr) << c.yaml;
		EXPECT_EQ(gtext_yaml_node_type(seq), GTEXT_YAML_SEQUENCE) << c.yaml;
		EXPECT_EQ(gtext_yaml_sequence_length(seq), c.count) << c.yaml;

		const GTEXT_YAML_Node *first = gtext_yaml_sequence_get(seq, 0);
		ASSERT_NE(first, nullptr) << c.yaml;
		const char *v = gtext_yaml_node_as_string(first);
		ASSERT_NE(v, nullptr) << c.yaml;
		EXPECT_STREQ(v, c.first) << c.yaml;

		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
}

/*
 * Not yet supported: a flow-context plain scalar containing spaces.  PyYAML
 * reads "[a - b, c]" as the two entries "a - b" and "c"; this scanner still
 * treats whitespace as a delimiter inside a flow collection, so that input
 * does not parse.  Block context gained multi-word plain scalars; flow context
 * has not.
 */
