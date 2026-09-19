/**
 * Block scalars: indentation (YAML 1.2.2 8.1.1.1), chomping (8.1.1.2) and
 * the literal and folded break rules (8.1.2, 8.1.3).
 *
 * Every expectation in the table is PyYAML's output for the same input, not a
 * transcription of the spec by hand. The scanner used to drop the final line
 * break under the default clip chomping, keep trailing empty lines that clip
 * discards, fold a blank line into one break too many, fold lines that are
 * more indented than the block instead of leaving their breaks alone, ignore
 * the indentation indicator, and swallow a leading blank line in the header.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <ghoti.io/text/yaml.h>

namespace {

struct BlockCase {
	const char *input;
	const char *expected;
};

const BlockCase kBlockCases[] = {
	{"a: |\n  block\n", "block\n"},
	{"a: |\n  block\n\n\n", "block\n"},
	{"a: |+\n  block\n", "block\n"},
	{"a: |+\n  block\n\n", "block\n\n"},
	{"a: |-\n  block\n", "block"},
	{"a: |-\n  block\n\n\n", "block"},
	{"a: |\n  one\n  two\n", "one\ntwo\n"},
	{"a: >\n  block\n", "block\n"},
	{"a: >-\n  block\n", "block"},
	{"a: >+\n  block\n\n", "block\n\n"},
	{"a: >\n  one\n  two\n", "one two\n"},
	{"a: >\n  one\n\n  two\n", "one\ntwo\n"},
	{"a: >\n  one\n\n\n  two\n", "one\n\ntwo\n"},
	{"a: >\n  one\n    deep\n  two\n", "one\n  deep\ntwo\n"},
	{"a: |\n", ""},
	{"a: |\n\n\n", ""},
	{"a: |+\n\n\n", "\n\n"},
	{"a: |2\n   text\n", " text\n"},
	{"a: |2\n  text\n", "text\n"},
	{"a: |-\n  one\n\n  two\n", "one\n\ntwo"},
	{"a: |\n  one\n\n  two\n", "one\n\ntwo\n"},
	{"a: >+\n  one\n\n", "one\n\n"},
	{"a: >-\n  one\n\n\n", "one"},
	{"a: |\n  a\n   b\n  c\n", "a\n b\nc\n"},
	{"a: >\n  a\n   b\n  c\n", "a\n b\nc\n"},
	{"a: |\n\n  text\n", "\ntext\n"},
	{"a: >\n\n  text\n", "\ntext\n"},
	{"a: |\n  one\nb: 2\n", "one\n"},
	{"a: >\n  one\n    deep\n    more\n  two\n", "one\n  deep\n  more\ntwo\n"},
	{"a: >\n  one\n    deep\n\n  two\n", "one\n  deep\n\ntwo\n"},
	{"a: >\n  one\n  two\n\n  three\n", "one two\nthree\n"},
	{"a: |\n \n  text\n", "\ntext\n"},
	{"a: >\n  one\n   \n  two\n", "one\n \ntwo\n"},
	{"a: |1\n  text\n", " text\n"},
	{"a: |3\n   text\n", "text\n"},
	{"a: |+\n  a\n\n\nb: 1\n", "a\n\n\n"},
	{"a: >\n  one\n    deep\n", "one\n  deep\n"},
	{"a: |\n  \ttab\n", "\ttab\n"},
};

std::string ScalarAt(const char *input, const char *key) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	EXPECT_NE(doc, nullptr) << "parse failed: " << (err.message ? err.message : "?");
	if (!doc) return "<parse-failed>";
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(gtext_yaml_document_root(doc), key);
	EXPECT_NE(value, nullptr) << "no key " << key;
	std::string out = value && gtext_yaml_node_as_string(value)
		? gtext_yaml_node_as_string(value) : "<missing>";
	gtext_yaml_free(doc);
	return out;
}

} // namespace

TEST(YamlBlockScalars, MatchesReferenceImplementation) {
	for (const BlockCase &c : kBlockCases) {
		EXPECT_EQ(ScalarAt(c.input, "a"), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* The indentation indicator counts from the parent node, so the same "2"
   strips a different number of spaces depending on how deeply the block sits.
   The scanner has no node stack, and reads the parent's indentation as the
   column of the first non-space character on the header's own line. */
TEST(YamlBlockScalars, IndentIndicatorCountsFromTheParent) {
	EXPECT_EQ(ScalarAt("a: |2\n     text\n", "a"), std::string("   text\n"));

	const char *nested = "a:\n  b: |2\n     text\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(nested, strlen(nested), nullptr, &err);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *outer = gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "a");
	ASSERT_NE(outer, nullptr);
	const GTEXT_YAML_Node *inner = gtext_yaml_mapping_get(outer, "b");
	ASSERT_NE(inner, nullptr);
	/* Parent "b" sits at column 2, so "|2" strips four spaces, not two. */
	EXPECT_STREQ(gtext_yaml_node_as_string(inner), " text\n");
	gtext_yaml_free(doc);
}

/* A block scalar under a sequence entry takes the dash's column as its
   parent, and one at the root takes zero. */
TEST(YamlBlockScalars, IndentIndicatorUnderASequenceEntry) {
	const char *input = "- |2\n   text\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(root), 1u);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(root, 0)), " text\n");
	gtext_yaml_free(doc);
}

/* Content is indented further than the node that owns it. A first non-empty
   line at or left of the parent leaves the scalar empty and belongs to
   whatever follows, rather than being swallowed. */
TEST(YamlBlockScalars, ContentLeftOfTheParentIsNotSwallowed) {
	const char *input = "a: |\nb: 2\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 2u);
	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(root, "b");
	ASSERT_NE(b, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(b), "2");
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
