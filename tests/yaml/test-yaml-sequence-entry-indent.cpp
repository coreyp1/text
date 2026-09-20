/**
 * Where a block sequence's entries may sit.
 *
 * l+block-sequence(n) is a run of s-indent(n) c-l-block-seq-entry(n), so
 * every entry of one sequence is at the same column and a deeper "-" has to
 * be some other node's value. There has to be something waiting to hold it,
 * and inside a sequence that is the entry indicator above it, still open:
 *
 *     -            the entry opened by "-" has no node yet, so the deeper
 *       - a        "-" starts the nested sequence that fills it.
 *
 *     - key: value the first entry already holds a mapping, so the deeper
 *      - item1     "-" belongs to nothing. This became a second entry
 *                  holding ["item1"] (suite case ZVH3).
 *
 * A "-" after a *scalar* entry never reaches this rule: it folds into that
 * scalar as plain content, which is why "- a" over " - b" is the one string
 * "a - b" and not a sequence at all. Both references agree on that and on
 * ZVH3.
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
	/* The entry is already full, so a deeper "-" has no owner. */
	{"- key: value\n - item1\n", nullptr},
	{"- {a: 1}\n - b\n", nullptr},

	/* The entry is still open, so a deeper "-" fills it. */
	{"-\n  - a\n", "[[\"a\"]]"},
	{"- - a\n", "[[\"a\"]]"},
	{"- - - a\n", "[[[\"a\"]]]"},
	{"-\n  -\n    - a\n", "[[[\"a\"]]]"},

	/* Entries of one sequence share a column. */
	{"- a\n- b\n", "[\"a\", \"b\"]"},
	{"- a\n-  b\n", "[\"a\", \"b\"]"},
	{"key:\n  - a\n  - b\n", "{\"key\": [\"a\", \"b\"]}"},

	/* Inside "[" or "{" there are only flow nodes, so a "-" is never an
	   entry indicator there. "[" over "- a" over "]" was building a block
	   sequence inside the flow one and giving [["a"]]; both references
	   refuse it, and the suite has no case either way. */
	{"[\n- a\n]\n", nullptr},
	{"[- a]\n", nullptr},
	{"[a, b]\n", "[\"a\", \"b\"]"},
	{"- [a]\n- b\n", "[[\"a\"], \"b\"]"},

	/* After a scalar entry a deeper "-" is that scalar's own content, so
	   these are one folded plain scalar rather than a sequence. */
	{"- a\n - b\n", "[\"a - b\"]"},
	{"- a\n  - b\n", "[\"a - b\"]"},
	{"key:\n  - a\n   - b\n", "{\"key\": [\"a - b\"]}"},
};

} // namespace

TEST(YamlSequenceEntryIndent, ADeeperEntryNeedsSomethingToHoldIt) {
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
