/**
 * Properties with no node in a flow collection (7.2).
 *
 * An anchor or tag names the node that follows it. When nothing follows,
 * that node is the empty node, and the properties belong to it: "!!str" with
 * no content is the empty string, an anchor with no content is an anchored
 * null. In block context the parser works this out from the line the
 * properties sit on relative to what comes next.
 *
 * Inside "[" or "{" neither the line nor the indentation says anything -
 * a flow collection is routinely written on one line - so every one of those
 * tests answered "same line, not left behind" and the properties were
 * carried past the end of the collection. Spec example 7.2
 *
 *     {
 *       foo : !!str,
 *       !!str : bar,
 *     }
 *
 * came back as {"foo": null, "bar": null} instead of {"foo": "", "": "bar"}
 * (suite case WZ62), and a property before a closing bracket leaked out of
 * the document entirely, reported as a second top-level node.
 *
 * What settles it there is the token instead: a ",", a ":" or the
 * collection's own closing bracket cannot be the node the properties name.
 *
 * Both references agree on the empty *key* spelling and on the anchors.
 * Neither accepts "{a: !!str}", where the tagged empty node is a value; the
 * suite carries both halves in one document and expects both.
 */
#include <gtest/gtest.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	/* Spec example 7.2 itself. */
	{"{\n  foo : !!str,\n  !!str : bar,\n}\n", "{\"foo\": \"\", \"\": \"bar\"}"},

	/* A tagged empty node before each of the three tokens that can end it. */
	{"{a: !!str}\n", "{\"a\": \"\"}"},              /* the closing brace */
	{"{a: !!str, b: 1}\n", "{\"a\": \"\", \"b\": 1}"}, /* a comma */
	{"{!!str : bar}\n", "{\"\": \"bar\"}"},         /* a colon, so it is a key */
	{"[!!str]\n", "[\"\"]"},
	{"[!!str, a]\n", "[\"\", \"a\"]"},

	/* An anchor with no node is an anchored null, not a missing entry. */
	{"{a: &x}\n", "{\"a\": null}"},
	{"[&x]\n", "[null]"},
	{"[&x, a]\n", "[null, \"a\"]"},

	/* When something does follow, the properties name it and there is no
	   empty node at all. */
	{"[!!str a]\n", "[\"a\"]"},
	{"{!!str a: b}\n", "{\"a\": \"b\"}"},
	{"[&x a]\n", "[\"a\"]"},

	/* Block context keeps its own rule, which is why the flow one asks
	   whether it is inside a collection at all. Here the anchor belongs to
	   the nested mapping that follows it, and the ":" on the next line must
	   not flush it as an empty node - that is suite case 26DV, which broke
	   when this rule was first written without the flow test. */
	{"a: &x\n  b : 1\n", "{\"a\": {\"b\": 1}}"},
	/* 26DV's own shape: the key on the indented line is an alias, so the
	   anchor above it has to survive to reach the mapping. */
	{"a: &alias1 x\ntop3: &node3\n  *alias1 : scalar3\n",
	 "{\"a\": \"x\", \"top3\": {\"x\": \"scalar3\"}}"},
	{"top3: &node3\n  key3 : scalar3\n",
	 "{\"top3\": {\"key3\": \"scalar3\"}}"},
	{"a: !!str\nb: 1\n", "{\"a\": \"\", \"b\": 1}"},

	/* Collections with no properties in them are untouched. */
	{"{a: 1}\n", "{\"a\": 1}"},
	{"[a, b]\n", "[\"a\", \"b\"]"},
	{"{a: 1, b: 2}\n", "{\"a\": 1, \"b\": 2}"},
};

} // namespace

TEST(YamlFlowEmptyNode, PropertiesWithNoNodeNameTheEmptyOne) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
