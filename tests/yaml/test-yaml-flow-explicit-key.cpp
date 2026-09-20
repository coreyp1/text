/**
 * An explicit "?" key inside a flow collection.
 *
 * ns-flow-pair, the thing a flow sequence entry may be, has two spellings
 * (7.4):
 *
 *     ns-flow-pair(n,c) ::=
 *         ( "?" s-separate(n,c) ns-flow-map-explicit-entry(n,c) )
 *       | ns-flow-pair-entry(n,c)
 *
 * The second - "[a: 1]" - was already handled: a ":" directly inside a flow
 * sequence opens a single-pair mapping that ends at the "," or "]" rather
 * than at a "}". The first was not. A "?" there fell through to the block
 * arm and pushed a *block* mapping inside the flow sequence, a level that
 * measures itself by indentation, which says nothing between "[" and "]".
 * It then swallowed the bracket meant to close the sequence, and
 *
 *     [
 *     ? foo
 *      bar : baz
 *     ]
 *
 * - spec example 7.20, suite case CT4Q - came back as a parse error
 * complaining about the indentation of a colon.
 *
 * The second half of this is that an explicit key is finished by whatever
 * ends its entry, not only by a ":". "[ ? a, ? b ]" and "{ ? a, ? b }" end
 * the first key at the ",". That was not being noticed, so the next "?"
 * read the key as one still waiting for a value and supplied a second null
 * behind the one already there: "{ ? a, ? b }" came out as
 * {"a": null, null: "b"}, which is not even the same shape. In a flow
 * sequence the stale depth pointed at a level about to be popped, so the
 * null landed in the sequence and the sequence itself was switched into
 * mapping-key state.
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
	/* Spec example 7.20 itself. The key is a multi-line plain scalar, which
	 * an explicit key may be - it is ns-flow-yaml-node, not the
	 * ns-s-implicit-yaml-key that "[a: 1]" restricts its key to. */
	{"[\n? foo\n bar : baz\n]\n", "[{\"foo bar\": \"baz\"}]"},

	/* The plain shapes. */
	{"[ ? a : 1 ]\n", "[{\"a\": 1}]"},
	{"[ ? a ]\n", "[{\"a\": null}]"},
	{"[ a, ? b : c, d ]\n", "[\"a\", {\"b\": \"c\"}, \"d\"]"},
	{"[ ? a : 1, ]\n", "[{\"a\": 1}]"},

	/* Each pair is its own entry of the sequence, not a shared mapping. */
	{"[ ? a : 1, ? b : 2 ]\n", "[{\"a\": 1}, {\"b\": 2}]"},
	{"[ ? a, ? b ]\n", "[{\"a\": null}, {\"b\": null}]"},
	{"[ ? a, b ]\n", "[{\"a\": null}, \"b\"]"},
	{"[ a, ? b ]\n", "[\"a\", {\"b\": null}]"},
	{"[ ? a, ? b : 2, c ]\n", "[{\"a\": null}, {\"b\": 2}, \"c\"]"},

	/* A "?" with nothing after it is still an entry:
	 * ns-flow-map-explicit-entry is "ns-flow-map-implicit-entry |
	 * ( e-node e-node )", so both halves are the empty node.  An empty
	 * mapping, [{}], is a different document and one the grammar cannot
	 * write here. */
	{"[ ? ]\n", "[{null: null}]"},

	/* The key may be a collection, and so may the value. */
	{"[ ? [1, 2] : 3 ]\n", "[{[1, 2]: 3}]"},
	{"[ ? {x: 1} : 2 ]\n", "[{{\"x\": 1}: 2}]"},
	{"[ ? a : [1, 2] ]\n", "[{\"a\": [1, 2]}]"},

	/* A collection *as* the key holds the explicit key open across its own
	 * commas.  Those commas end entries of the inner collection, not the
	 * outer key, which is why the entry-completed rule asks about the level
	 * it is on before it clears anything.
	 *
	 * A block mapping is where that matters most.  "? {a, b}" over ": 1" has
	 * the "," two levels below the key it would otherwise have finished, and
	 * finishing it there leaves the ":" on the next line looking for a key
	 * that is no longer claimed - the parser then refuses a valid document,
	 * complaining that the key is not on the colon's line. */
	{"? {a, b}\n: 1\n", "{{\"a\": null, \"b\": null}: 1}"},
	{"? {a, b}\n", "{{\"a\": null, \"b\": null}: null}"},
	{"? !!str {a, b}\n: 1\n", "{{\"a\": null, \"b\": null}: 1}"},
	{"a:\n  ? {x, y}\n  : 1\n", "{\"a\": {{\"x\": null, \"y\": null}: 1}}"},
	{"? {a, b}\n: 1\n? {c, d}\n: 2\n",
	 "{{\"a\": null, \"b\": null}: 1, {\"c\": null, \"d\": null}: 2}"},

	/* Same question for the other half: a flow pair closing inside a key that
	 * a "?" is still waiting for.  The pair "a: 1" here ends at the "]", and
	 * the empty node that a keyless "?" is owed belongs to the outer "?",
	 * not to this pair - which would otherwise come back as [{null: null}]
	 * with the real pair pushed aside. */
	{"? [ a: 1 ]\n: 2\n", "{[{\"a\": 1}]: 2}"},
	{"? [ a: 1 ]\n", "{[{\"a\": 1}]: null}"},
	{"? { k: [b: 2] }\n: 3\n", "{{\"k\": [{\"b\": 2}]}: 3}"},

	{"[ ? {b, c} : 1 ]\n", "[{{\"b\": null, \"c\": null}: 1}]"},
	{"[ ? {b, c} ]\n", "[{{\"b\": null, \"c\": null}: null}]"},
	{"{ ? {b, c} }\n", "{{\"b\": null, \"c\": null}: null}"},
	{"{ ? {b, c} : 1 }\n", "{{\"b\": null, \"c\": null}: 1}"},
	{"[ ? [b, c] : 1 ]\n", "[{[\"b\", \"c\"]: 1}]"},

	/* Nesting keeps working, which is the part that broke when a block level
	 * was pushed inside the flow one. */
	{"[ [ ? a : 1 ] ]\n", "[[{\"a\": 1}]]"},
	{"{ k: [ ? a : 1 ] }\n", "{\"k\": [{\"a\": 1}]}"},

	/* In a flow mapping the "?" opens no new level - the mapping is already
	 * there - but the key still ends at the ",". */
	{"{ ? a : 1 }\n", "{\"a\": 1}"},
	{"{ ? a }\n", "{\"a\": null}"},
	{"{ ? a, ? b }\n", "{\"a\": null, \"b\": null}"},
	{"{ ? a, b: 1 }\n", "{\"a\": null, \"b\": 1}"},
	{"{ ? a : 1, ? b : 2 }\n", "{\"a\": 1, \"b\": 2}"},
	{"{ ? a : 1, b }\n", "{\"a\": 1, \"b\": null}"},

	/* "?" is an indicator only where a separation follows it.  "?a" is an
	 * ordinary plain scalar (ns-plain-first, 7.3.3). */
	{"[ ?a ]\n", "[\"?a\"]"},

	/* Nothing above should have loosened the entries that are not pairs. */
	{"[ a, b ]\n", "[\"a\", \"b\"]"},
	{"{ a, b }\n", "{\"a\": null, \"b\": null}"},
	{"[ foo: bar ]\n", "[{\"foo\": \"bar\"}]"},

	/* A pair holds one ":" and the entry ends at the "," or the "]". */
	{"[ ? a : 1 : 2 ]\n", nullptr},
};

} // namespace

TEST(YamlFlowExplicitKey, PairsInsideFlowCollections) {
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
