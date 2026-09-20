/**
 * What an explicit key's value is measured against.
 *
 * A ":" usually belongs to the node its key began, which is not the start of
 * the line: in "- x: 1" the key sits at column 2 while the line begins at 0,
 * and a plain scalar after the ":" continues only while later lines are
 * indented past the key.
 *
 * An explicit key's value is written differently. c-l-block-map-explicit-value(n)
 * is s-indent(n) ":" ... (8.2.2), so the ":" stands at the *mapping's*
 * indentation with no key in front of it, and the value that follows is
 * measured against that. Taking the key's column instead compared the value's
 * continuation lines with the wrong node, so in
 *
 *     ? a
 *       true
 *     : null
 *       d
 *     ? e
 *       42
 *
 * the "  d" was not indented past what it was being compared with. It did not
 * fold into "null d", arrived as a scalar of its own, and was refused for
 * being deeper than its mapping with no key to hold it. Suite case JTV5.
 *
 * The rule is simply which line the ":" opens: its own, or one a key already
 * started.
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
	/* The case itself: multi-line plain scalars on both sides. */
	{"? a\n  true\n: null\n  d\n? e\n  42\n",
	 "{\"a true\": \"null d\", \"e 42\": null}"},

	/* Each half on its own. */
	{"? a\n  true\n: b\n", "{\"a true\": \"b\"}"},
	{"? a\n: null\n  d\n", "{\"a\": \"null d\"}"},
	{"? a\n: one\n  two\n  three\n", "{\"a\": \"one two three\"}"},

	/* A value that dedents back to the mapping is not a continuation. */
	{"? a\n: b\nc: d\n", "{\"a\": \"b\", \"c\": \"d\"}"},

	/* Nested one level in, where the mapping's indentation is not zero. */
	{"m:\n  ? a\n    true\n  : null\n    d\n",
	 "{\"m\": {\"a true\": \"null d\"}}"},

	/* A ":" that a key opened still belongs to that key, which is the rule
	 * this one had to be carved out of. */
	{"- x: 1\n", "[{\"x\": 1}]"},
	{"a: b\n", "{\"a\": \"b\"}"},
	{"- x: one\n    two\n", "[{\"x\": \"one two\"}]"},
	{"a: one\n  two\n", "{\"a\": \"one two\"}"},

	/* A sequence entry's own mapping, where the key is not at column 0. */
	{"- ? a\n    true\n  : b\n", "[{\"a true\": \"b\"}]"},

	/* And the ordinary explicit-key shapes. */
	{"? a\n: b\n", "{\"a\": \"b\"}"},
	{"? a\n", "{\"a\": null}"},
	{"? a\n? b\n", "{\"a\": null, \"b\": null}"},
};

} // namespace

TEST(YamlExplicitKeyValueIndent, TheColonThatOpensItsOwnLine) {
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
