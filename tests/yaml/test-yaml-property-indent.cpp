/**
 * Where an anchor or tag may be written.
 *
 * c-ns-properties reaches a block collection only through s-separate(n+1,c)
 * (8.2), so properties have to be indented past every block collection that
 * was already open when they were written.  Three shapes went through anyway,
 * each of them naming a node that does not exist:
 *
 *     - item1        "&node" is at the sequence's own indentation, so it
 *     &node          introduces nothing; the anchor was quietly carried
 *     - item2        onto "item2" instead (suite case GT5M).
 *
 *     seq:           the sequence is the value of "seq", but "&anchor" is at
 *     &anchor        the mapping's indentation rather than past it, and it
 *     - a            ended up on "a" (G9HC).
 *
 *     key: &x        "!!map" is not indented past the mapping, so the value
 *     !!map          of "key" is the empty node that "&x" names and the
 *       a: b         mapping below it has no owner (H7J7).
 *
 * The exception is a mapping key, which sits at its mapping's indentation and
 * carries its properties there: "!!str 23: !!bool false" is a well-formed
 * entry of a mapping at column 0.  An implicit key has to fit on one line, so
 * the key's properties are always on the key's own line - which is what tells
 * the two cases apart.
 *
 * js-yaml accepts H7J7; PyYAML refuses it, and so does yaml-test-suite. Both
 * references agree with the suite on everything else here.
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
	/* A property on a line of its own, at the indentation of a collection
	   that is already open: there is no node for it to name. */
	{"- item1\n&node\n- item2\n", nullptr},
	{"seq:\n&anchor\n- a\n- b\n", nullptr},
	{"key: &x\n!!map\n  a: b\n", nullptr},
	{"a: 1\n!!map\nb: 2\n", nullptr},
	{"a: 1\n&x\nb: 2\n", nullptr},

	/* Nothing is open yet, so the property introduces the whole document. */
	{"!!seq\n- a\n", "[\"a\"]"},
	{"&a\n- x\n", "[\"x\"]"},
	{"!!map\na: b\n", "{\"a\": \"b\"}"},
	{"--- &anchor\na: b\n", "{\"a\": \"b\"}"},

	/* Indented past what is open, so it introduces the value. */
	{"key:\n  &a\n  - x\n", "{\"key\": [\"x\"]}"},
	{"key:\n  !!seq\n  - x\n", "{\"key\": [\"x\"]}"},

	/* On the node's own line, so the rule does not apply. */
	{"- &a b\n", "[\"b\"]"},
	{"- &a\n  b\n", "[\"b\"]"},
	{"key: &a\n- a\n", "{\"key\": [\"a\"]}"},
	{"a: &x 1\n", "{\"a\": 1}"},

	/* A key carries its properties at the mapping's own indentation. */
	{"!!str a: b\n", "{\"a\": \"b\"}"},
	{"a: b\n!!str 23: c\n", "{\"a\": \"b\", \"23\": \"c\"}"},
	{"a: 1\n&anchor c: 3\n", "{\"a\": 1, \"c\": 3}"},

	/* A node may carry both an anchor and a tag, on separate lines.  Every
	   one of them has to clear the indentation, so it is the leftmost that
	   decides - here the anchor, even though the tag below it is indented. */
	{"a: 1\n&x\n  !!str y\n", nullptr},
	{"seq:\n&x\n  !!seq\n  - a\n", nullptr},
	{"seq:\n&x\n  - a\n", nullptr},
	{"seq:\n  !!seq\n  - a\n", "{\"seq\": [\"a\"]}"},
	{"a: 1\n  !!str\n&x\ny\n", nullptr},
	{"key:\n  &a\n  !!str\n  x\n", "{\"key\": \"x\"}"},

	/* Indentation constrains nothing inside a flow collection. */
	{"[\n&a x\n]\n", "[\"x\"]"},
};

} // namespace

TEST(YamlPropertyIndent, MustBeIndentedPastAnOpenCollection) {
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
