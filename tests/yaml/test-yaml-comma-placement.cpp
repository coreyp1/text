/**
 * Where a "," may appear.
 *
 * It belongs to a flow collection and nowhere else: c-flow-sequence and
 * c-flow-mapping are the only productions that hold one (7.4), and
 * ns-plain-first excludes c-indicator, so a plain scalar cannot begin with
 * one either (7.3.3). Outside "[" or "{" the parser simply ignored it, which
 * is how
 *
 *     - !!str, xxx
 *
 * came back as the one-entry sequence ["xxx"] (suite case U99R). The tag
 * name stops at the "," correctly - ns-tag-char excludes c-flow-indicator -
 * and then the "," itself vanished, leaving a well-formed-looking document
 * built out of an ill-formed one.
 *
 * A "," *inside* a plain scalar is a different thing. ns-plain-char allows
 * it in block context, so "a,b" is one scalar and "1,2,3" is one string,
 * which is why the rule is about where a node begins rather than about the
 * character.
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
	/* Nothing here opens a flow collection. */
	{"- !!str, xxx\n", nullptr},
	{"- ,foo\n", nullptr},
	{"a: ,b\n", nullptr},
	{",\n", nullptr},

	/* Inside a plain scalar it is ordinary content. */
	{"a: b,c\n", "{\"a\": \"b,c\"}"},
	{"- a,b\n", "[\"a,b\"]"},
	{"a,b: c\n", "{\"a,b\": \"c\"}"},
	{"- 1,2,3\n", "[\"1,2,3\"]"},
	{"a: \"x,y\"\n", "{\"a\": \"x,y\"}"},

	/* And inside a flow collection it is the separator it is meant to be. */
	{"[a, b]\n", "[\"a\", \"b\"]"},
	{"{a: 1, b: 2}\n", "{\"a\": 1, \"b\": 2}"},
	{"key: [1, 2]\n", "{\"key\": [1, 2]}"},

	/* The tag without the comma is the document U99R is a corruption of. */
	{"- !!str xxx\n", "[\"xxx\"]"},
};

} // namespace

TEST(YamlCommaPlacement, OnlyInsideAFlowCollection) {
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
