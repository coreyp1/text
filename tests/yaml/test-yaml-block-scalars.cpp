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
#include <string>
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

namespace {

/* The whole document as one scalar, for block scalars that are the root. */
std::string RootScalar(const char *input) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	EXPECT_NE(doc, nullptr) << "parse failed: " << (err.message ? err.message : "?");
	if (!doc) return "<parse-failed>";
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	std::string out = root && gtext_yaml_node_as_string(root)
		? gtext_yaml_node_as_string(root) : "<missing>";
	gtext_yaml_free(doc);
	return out;
}

}  // namespace

/* A block scalar that is the document's root has no owning node, and the
   spec's n is -1 there (9.1.2 l-bare-document, and the n+m in 8.1.2/8.1.3).
   The scanner took the header's own column instead, so a root block scalar
   demanded content indented past column 0 and collected nothing: "--- >"
   over three lines at column 0 returned the three lines as separate nodes,
   which is now also a refusal rather than a wrong answer. */
TEST(YamlBlockScalars, ARootBlockScalarHasNoParentIndentation) {
	EXPECT_EQ(RootScalar("--- >\nline1\nline2\nline3\n"),
		std::string("line1 line2 line3\n"));
	EXPECT_EQ(RootScalar("--- |\na\nb\n"), std::string("a\nb\n"));
	EXPECT_EQ(RootScalar(">\nfoo\nbar\n"), std::string("foo bar\n"));
	/* With an indicator the content sits at n+m, so m=1 means column 0 and
	   the one space on the line is content. PyYAML says "a\n" here; js-yaml
	   and the spec's own arithmetic say " a\n". */
	EXPECT_EQ(RootScalar("--- >1\n a\n"), std::string(" a\n"));
}

/* The header's two indicators may come in either order (8.1.1). Reading the
   sign first and the digits second left the sign of ">1-" to be swallowed as
   part of the trailing comment, so the scalar was chomped clip. */
TEST(YamlBlockScalars, HeaderIndicatorsComeInEitherOrder) {
	EXPECT_EQ(ScalarAt("a: >1-\n  strip\n\n", "a"), std::string(" strip"));
	EXPECT_EQ(ScalarAt("a: >-1\n  strip\n\n", "a"), std::string(" strip"));
	EXPECT_EQ(ScalarAt("a: |+2\n   x\n\n", "a"), std::string(" x\n\n"));
	EXPECT_EQ(ScalarAt("a: |2+\n   x\n\n", "a"), std::string(" x\n\n"));
}

/* And a malformed header is refused rather than read as something. The
   indentation indicator is one digit and not zero (8.1.1.1,
   c-indentation-indicator is ns-dec-digit minus "0"), there is at most one
   chomping indicator, and only a comment may follow. "|0" was being read as
   an indentation of zero and "|10" as ten. */
TEST(YamlBlockScalars, RefusesAMalformedHeader) {
	static const char *const kBad[] = {
		"a: |0\n x\n",     /* zero is not a valid indentation indicator */
		"a: |10\n x\n",    /* nor is a second digit */
		"a: |+-\n x\n",    /* two chomping indicators */
		"a: |-+\n x\n",
		"a: |1 2\n x\n",   /* the indicator is not repeatable either */
		"a: |x\n x\n",     /* only a comment may follow the header */
	};
	for (const char *input : kBad) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), nullptr, &err);
		EXPECT_EQ(doc, nullptr) << input;
		gtext_yaml_free(doc);
	}

	/* What still has to parse. */
	EXPECT_EQ(ScalarAt("a: | # comment\n  x\n", "a"), std::string("x\n"));
	EXPECT_EQ(ScalarAt("a: |9\n         x\n", "a"), std::string("x\n"));
	EXPECT_EQ(ScalarAt("a: |-\n  x\n", "a"), std::string("x"));
	EXPECT_EQ(ScalarAt("a: |\n  x\n", "a"), std::string("x\n"));
}

/* Folding (8.1.3): a run of empty lines beside a more-indented line yields
   one break more than it has empty lines - the b-as-line-feed that separates
   two l-nb-same-lines groups, or b-l-spaced's own break. Only the line
   *before* the run was checked, so a blank line in front of a more-indented
   one lost a break and spec example 2.15 came back with its indented block
   pulled up against the paragraph above it. */
TEST(YamlBlockScalars, ABlankLineBesideAMoreIndentedLineKeepsItsBreak) {
	EXPECT_EQ(RootScalar(">\n a\n\n   b\n"), std::string("a\n\n  b\n"));
	EXPECT_EQ(RootScalar(">\n a\n\n\n   b\n"), std::string("a\n\n\n  b\n"));
	/* Unchanged: two ordinary lines fold to a space, and a run of empty
	   lines between them to one break each. */
	EXPECT_EQ(RootScalar(">\n a\n b\n"), std::string("a b\n"));
	EXPECT_EQ(RootScalar(">\n a\n\n b\n"), std::string("a\nb\n"));
	EXPECT_EQ(RootScalar(">\n a\n   b\n"), std::string("a\n  b\n"));
	EXPECT_EQ(RootScalar(">\n   a\n\n\n   b\n"), std::string("a\n\nb\n"));
	/* Spec example 6.7, which is where the rule came from: a line of only
	   white space is an empty line even though the next line is
	   more-indented, and both breaks survive. */
	EXPECT_EQ(RootScalar(">\n  foo \n \n  \t bar\n\n  baz\n"),
		std::string("foo \n\n\t bar\n\nbaz\n"));
}

/* 8.1.1.1: "It is an error for any of the leading empty lines to contain
   more spaces than the first non-empty line." Without the rule there is no
   telling which indentation the block meant, and a leading run of wider
   blank lines was being taken as content - suite case S98Z, where a folded
   scalar over three blank lines and a comment came back holding all four. */
TEST(YamlBlockScalars, RefusesALeadingEmptyLineIndentedPastTheBlock) {
	static const char *const kRefused[] = {
		"a: >\n \n  \n   \n # c\n",
		"a: |\n    \n  x\n",
	};
	for (const char *input : kRefused) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), nullptr, &err);
		EXPECT_EQ(doc, nullptr) << input;
		gtext_yaml_free(doc);
	}

	/* A leading empty line no wider than the block is ordinary, and stands
	   for one break. */
	EXPECT_EQ(ScalarAt("a: |\n\n  x\n", "a"), std::string("\nx\n"));
	EXPECT_EQ(ScalarAt("a: |\n \n  x\n", "a"), std::string("\nx\n"));
	EXPECT_EQ(ScalarAt("a: |\n  \n  x\n", "a"), std::string("\nx\n"));
	/* And a blank line in the middle is not a leading one. */
	EXPECT_EQ(ScalarAt("a: |\n  x\n\n  y\n", "a"), std::string("x\n\ny\n"));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
