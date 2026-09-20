/**
 * The empty node (7.2, e-node).
 *
 * A position that takes a node and holds nothing is the empty node, which
 * resolves to null - or to whatever a tag on it says, so "!!str" with no
 * content is the empty string. Every one of these was being dropped instead:
 *
 *     -               was []                 rather than [null]
 *     - # Empty       was ["a"]              rather than [null, "a"]
 *     - a
 *     - a             was ["a"]              rather than ["a", ""]
 *     - !!str
 *     a: &anchor      was {a: null, b: "b"}  rather than {a: null, b: null}
 *     b: *anchor
 *
 * The last one is the worst: the anchor had no node, so the stream handed it
 * to the next one it saw, and b ended up anchored to itself.
 *
 * Telling an unclaimed property from one that introduces the collection
 * below it is a question of where the lines begin - a property that opened
 * its own line introduces whatever follows, and one written part way along a
 * line is left behind by a line that begins no further right.
 *
 * Expectations are js-yaml's, which agree with PyYAML throughout.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	{"-\n", "[null]"},  /* an entry with no node is the empty node */
	{"- a\n-\n- c\n", "[\"a\", null, \"c\"]"},  /* in the middle of a sequence */
	{"- # Empty\n- a\n", "[null, \"a\"]"},  /* a comment does not fill it */
	{"- &a\n- b\n", "[null, \"b\"]"},  /* nor does an anchor */
	{"- a\n- !!str\n", "[\"a\", \"\"]"},  /* but a tag decides what the empty node is */
	{"a: !!str\nb: 1\n", "{\"a\": \"\", \"b\": 1}"},  /* as a mapping value too */
	{"a: &anchor\nb: *anchor\n", "{\"a\": null, \"b\": null}"},  /* and the anchor names it, so the alias finds null */
	{"- !!str \"a\"\n- 'b'\n- &x \"c\"\n- *x\n- !!str\n", "[\"a\", \"b\", \"c\", \"c\", \"\"]"},  /* spec example 7.24 */
	{"- # Empty\n- |\n block node\n- - one # Compact\n  - two # sequence\n- one: two # Compact mapping\n", "[null, \"block node\\n\", [\"one\", \"two\"], {\"one\": \"two\"}]"},  /* spec example 8.15 */
	{"- a\n- b\n", "[\"a\", \"b\"]"},  /* unchanged: two ordinary entries */
	{"- - a\n- b\n", "[[\"a\"], \"b\"]"},  /* a nested sequence is not an empty entry */
	{"a:\n- 1\n- 2\n", "{\"a\": [1, 2]}"},  /* nor is a sequence at its key's column */
	{"!!seq\n- a\n- b\n", "[\"a\", \"b\"]"},  /* a tag on its own line introduces the collection */
	{"--- !!seq\n- a\n", "[\"a\"]"},  /* even after a document marker */
	{"x: !custom\n  - 1\n", "{\"x\": [1]}"},  /* and one on a key's line introduces the indented value */
	{"!!map\na: 1\n", "{\"a\": 1}"},  /* likewise for a mapping */
	/* A block sequence may stand at the column of the key that owns it, so
	   a "-" there takes the tag for the sequence rather than leaving it on
	   an empty value - spec example 8.22. */
	{"sequence: !!seq\n- entry\n- !!seq\n - nested\nmapping: !!map\n foo: bar\n",
	 "{\"sequence\": [\"entry\", [\"nested\"]], \"mapping\": {\"foo\": \"bar\"}}"},
	/* A line of nothing but properties introduces what follows, however
	   many of them there are - suite case 9KAX. */
	{"&a4 !!map\n&a5 !!str key5: value4\n", "{\"key5\": \"value4\"}"},
	{"---\n&a1\n!!str\nscalar1\n", "\"scalar1\""},
};

}  // namespace

TEST(YamlEmptyNode, IsNullRatherThanNothing) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
