/**
 * A ":" with no key in front of it.
 *
 * The empty node is a node. c-ns-flow-map-empty-key-entry is
 *
 *     e-node c-ns-flow-map-separate-value(n,c)
 *
 * (7.4), so "{ : 1 }" is {null: 1}; "? " with no key is the same idea in
 * block context, where c-l-block-map-explicit-entry reaches its key through
 * e-node (8.2.2).
 *
 * None of that was happening, and the flow-sequence case was the worst of
 * it. A ":" inside "[" and "]" opens a single-pair mapping and took its key
 * from wherever the last node happened to be - which meant reaching back
 * across a comma. So
 *
 *     [a, : 1]
 *
 * came back as the one-entry sequence [{"a": 1}] instead of the two-entry
 * ["a", {null: 1}]. Nothing was reported. An entry disappeared, a pair
 * nobody wrote appeared, and the document quietly meant something else.
 * FLOW_ITEM_DONE is what says whether an entry has actually started since
 * the last separator, and the key is taken only when it has.
 *
 * The flow-mapping half was wrong differently: with no key waiting, the
 * value landed in the key slot and collected a null of its own, so
 * "{ : 1 }" came out as {1: null} - neither the right key nor the right
 * value.
 *
 * "[ , ]" is still an error. An entry that holds nothing at all is not the
 * same as one that holds the empty node: the "?" or the ":" is what writes
 * it down.
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
	/* The case that corrupted a document rather than refusing it. */
	{"[a, : 1]\n", "[\"a\", {null: 1}]"},
	{"[a,\n: 1]\n", "[\"a\", {null: 1}]"},
	{"[a, b: 2]\n", "[\"a\", {\"b\": 2}]"},

	/* A ":" that opens the sequence's first entry. */
	{"[: 1]\n", "[{null: 1}]"},
	{"[{: 1}]\n", "[{null: 1}]"},

	/* A flow mapping's empty key. */
	{"{ : 1 }\n", "{null: 1}"},
	{"{a: 1, : 2}\n", "{\"a\": 1, null: 2}"},
	{"{: }\n", "{null: null}"},
	{"{a: , : b}\n", "{\"a\": null, null: \"b\"}"},

	/* An explicit key that never arrived is the empty node too, in flow
	 * and in block. */
	{"{ ? : 1 }\n", "{null: 1}"},
	{"[ ? : 1 ]\n", "[{null: 1}]"},
	{"?\n: 1\n", "{null: 1}"},
	{"{ ? }\n", "{null: null}"},
	{"[ ? ]\n", "[{null: null}]"},

	/* A "?" starts an entry before its key arrives, so the "," after it has
	 * something in front of it. */
	{"[ ? , ? ]\n", "[{null: null}, {null: null}]"},
	{"[ ? a, ? ]\n", "[{\"a\": null}, {null: null}]"},
	{"{ ? , a }\n", "{null: null, \"a\": null}"},
	{"{ ? a, ? }\n", "{\"a\": null, null: null}"},
	{"{ ? , ? a }\n", "{null: null, \"a\": null}"},

	/* Nothing above should have loosened the ordinary shapes. */
	{"[a: 1]\n", "[{\"a\": 1}]"},
	{"[[1]: 2]\n", "[{[1]: 2}]"},
	{"[a, b, c]\n", "[\"a\", \"b\", \"c\"]"},
	{"{a: 1, b: 2}\n", "{\"a\": 1, \"b\": 2}"},
	{"{a, b}\n", "{\"a\": null, \"b\": null}"},
	{"{a: }\n", "{\"a\": null}"},
	{"[a: ]\n", "[{\"a\": null}]"},
	{"{}\n", "{}"},
	{"[]\n", "[]"},

	/* An entry with nothing in it at all, which no production writes.  A "?"
	 * further out does not excuse one: the empty entry below belongs to the
	 * inner collection, not to the explicit key holding it. */
	{"? [ , ]\n: 1\n", nullptr},
	{"? { , a }\n: 1\n", nullptr},
	{"[ ? [ , ] ]\n", nullptr},
	{"{ ? [ , ] }\n", nullptr},
	{"? [a, , b]\n: 1\n", nullptr},

	{"[ , ]\n", nullptr},
	{"[ , a ]\n", nullptr},
	{"{ , a }\n", nullptr},
	{"[ a, , b ]\n", nullptr},

	/* Two colons in one pair is still two entries with no "," between. */
	{"{a: 1: 2}\n", nullptr},
	{"[a: 1: 2]\n", nullptr},

	/* An implicit key shares its colon's line.  The empty key has no line to
	 * disagree with, but a written one does, and that rule stands. */
	{"[a\n: 1]\n", nullptr},

	/* Two empty keys in one mapping are two identical keys. */
	{"{ ? , ? }\n", nullptr},
};

} // namespace

TEST(YamlEmptyKey, ColonWithNoKeyTakesTheEmptyNode) {
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
