/**
 * How far in a multi-line quoted scalar's continuation lines have to be.
 *
 * s-flow-folded(n) puts s-indent(n) in front of every line after the first
 * (6.5), and the flow node sits one level in from the block node that owns
 * it - s-l+flow-in-block(n) is s-separate(n+1,c) ns-flow-node(n+1,c). So
 * under a mapping at column 0 a continuation line needs at least one space,
 * and
 *
 *     quoted: "a
 *     b
 *     c"
 *
 * is not a scalar spanning three lines; it came back as "a b c" (suite case
 * QB6E). At the root there is no owning node - n is -1 - so column 0 is
 * already indented past it and the same shape parses.
 *
 * Both references accept every case in this file that the suite does not
 * cover, including the closing quote alone at column 0 and a sequence
 * entry's scalar continued at column 0. The grammar does not, and the suite
 * sides with the grammar on the one case it does carry.
 *
 * Empty lines are not measured. They fold to line feeds and carry no
 * indentation to compare, so only the line that ends the fold decides.
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
	/* Under a mapping at column 0, a continuation line needs one space. */
	{"quoted: \"a\nb\nc\"\n", nullptr},
	{"quoted: \"a\n b\"\n", "{\"quoted\": \"a b\"}"},
	{"quoted: \"a\n  b\n  c\"\n", "{\"quoted\": \"a b c\"}"},
	{"quoted: 'a\nb'\n", nullptr},
	{"quoted: 'a\n b'\n", "{\"quoted\": \"a b\"}"},

	/* The line holding only the closing quote is a continuation line too. */
	{"k: \"a\n  b\n\"\n", nullptr},
	{"k: \"a\n  b\n  \"\n", "{\"k\": \"a b \"}"},

	/* A sequence entry's node is one level in as well. */
	{"- \"a\nb\"\n", nullptr},
	{"- \"a\n  b\"\n", "[\"a b\"]"},

	/* At the root there is no owning node, so column 0 clears it. */
	{"\"a\nb\"\n", "\"a b\""},
	{"'a\nb'\n", "\"a b\""},

	/* Nested deeper: the value of a key at column 2 needs three. */
	{"k:\n  j: \"a\n   b\"\n", "{\"k\": {\"j\": \"a b\"}}"},
	{"k:\n  j: \"a\n b\"\n", nullptr},

	/* Empty lines carry no indentation and are not measured; the line that
	   ends the fold is. */
	{"k: \"a\n\n  b\"\n", "{\"k\": \"a\\nb\"}"},
	{"k: \"a\n\nb\"\n", nullptr},

	/* Indentation is spaces, never tabs (6.1). A tab on a continuation line
	   is separation, so it ends the indentation rather than counting toward
	   it: a leading tab leaves the line at column 0. A space first, then a
	   tab, is one column of indentation and then separation. Both references
	   accept every one of these; the suite has the first as an error
	   (DK95/1). */
	{"foo: \"bar\n\tbaz\"\n", nullptr},
	{"foo: \"bar\n\t baz\"\n", nullptr},
	{"foo: \"bar\n \tbaz\"\n", "{\"foo\": \"bar baz\"}"},
	{"foo: \"bar\n  \tbaz\"\n", "{\"foo\": \"bar baz\"}"},
	/* At the root there is nothing to clear, so even column 0 is enough. */
	{"\"bar\n\tbaz\"\n", "\"bar baz\""},
	/* The tab ends the indentation for good: spaces after it are separation
	   too, so this line is indented one column and not three. Under a key at
	   column 2 that is not enough. */
	{"k:\n  j: \"a\n \t  b\"\n", nullptr},

	/* An escaped break folds through the same code. */
	{"k: \"a\\\n  b\"\n", "{\"k\": \"ab\"}"},
	{"k: \"a\\\nb\"\n", nullptr},
};

} // namespace

TEST(YamlScalarContinuationIndent, MustBeIndentedPastTheOwningNode) {
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
