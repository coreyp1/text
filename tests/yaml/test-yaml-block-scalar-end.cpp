/**
 * Where a block scalar stops, and what its indentation is when nothing says.
 *
 * Two rules, both about the same loop.
 *
 * A "---" or "..." at column 1 ends the document wherever it stands (9.1.2,
 * 9.2), and no block scalar reaches past one. The loop ends a block scalar at
 * a line indented less than the block, which covers every indented case - but
 * a block scalar written at the document level has indentation zero, and
 * nothing is less than that. So
 *
 *     --- |
 *     abc
 *     ...
 *     --- |
 *     def
 *
 * came back as the single scalar "abc\n...\n--- |\ndef\n": two documents
 * merged into one, silently. Spec example 9.5 is the suite's version of it
 * (W4TN). The marker only counts at column 1, so an indented "..." inside a
 * block scalar is still content.
 *
 * The other rule is what a block scalar's indentation is when it holds
 * nothing but empty lines. 8.1.1.1 detects it from the first non-empty line,
 * and there is none, so the widest of the empty lines is the answer: every
 * line is then indentation and the block is a run of breaks. Zero made those
 * spaces content, so "- |+" over a line of three spaces gave ["   \n"] where
 * both references give ["\n"] (suite case JEF9).
 */
#include <gtest/gtest.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* A document marker ends the block scalar at column 1. */
	{"--- |\nabc\n...\n", "\"abc\\n\""},
	{"|\nabc\n...\n", "\"abc\\n\""},
	{"--- |\nabc\n---\ndef\n", "\"abc\\n\""},
	{"--- >\nabc\n...\n", "\"abc\\n\""},

	/* Indented, it is ordinary content: c-forbidden is about column 1. */
	{"a: |\n  ...\n  x\n", "{\"a\": \"...\\nx\\n\"}"},
	{"a: |\n  ---\n  x\n", "{\"a\": \"---\\nx\\n\"}"},
	{"a: |\n  ...x\n", "{\"a\": \"...x\\n\"}"},

	/* "..." needs white space or the end after it to be a marker at all. */
	{"--- |\nabc\n...x\n", "\"abc\\n...x\\n\""},

	/* A block scalar of nothing but empty lines. */
	{"- |+\n   \n", "[\"\\n\"]"},
	{"- |+\n\n\n", "[\"\\n\\n\"]"},
	{"- |+\n  \n    \n", "[\"\\n\\n\"]"},
	{"a: |\n   \n", "{\"a\": \"\"}"},
	{"a: |+\n  \n", "{\"a\": \"\\n\"}"},

	/* Once a non-empty line sets the indentation, extra spaces on a later
	 * line are content again. */
	{"a: |\n  x\n     \n  y\n", "{\"a\": \"x\\n   \\ny\\n\"}"},
	{"a: |\n  x\n\n  y\n", "{\"a\": \"x\\n\\ny\\n\"}"},

	/* Ordinary block scalars, untouched. */
	{"a: |\n  one\n  two\n", "{\"a\": \"one\\ntwo\\n\"}"},
	{"a: >\n  one\n  two\n", "{\"a\": \"one two\\n\"}"},
	{"a: |-\n  one\n", "{\"a\": \"one\"}"},
	{"a: |2\n    one\n", "{\"a\": \"  one\\n\"}"},
};

} // namespace

TEST(YamlBlockScalarEnd, MarkersAndEmptyLineIndentation) {
	for (const Case &c : kCases) {
		const std::string got = Render(c.input);
		if (c.expected) {
			EXPECT_EQ(got, std::string(c.expected))
				<< "input: " << ::testing::PrintToString(std::string(c.input));
		} else {
			EXPECT_EQ(got, std::string(""))
				<< "should have been refused, input: "
				<< ::testing::PrintToString(std::string(c.input));
		}
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
