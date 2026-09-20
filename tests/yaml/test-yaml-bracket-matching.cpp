/**
 * A closing bracket has to close something, and the right something.
 *
 * "]" ends the flow sequence a "[" opened and "}" ends the flow mapping a
 * "{" opened (7.4). Neither end event asked whether such a collection was
 * open, or of which kind, so the children of whatever collection happened to
 * be on the stack were handed to the node constructor the bracket named:
 *
 *     a: 1        was  ["a", 1]         a mapping flattened into a sequence
 *     ]
 *
 *     {a: 1]      was  ["a", 1]         mismatched, and reshaped
 *     [1, 2}      was  {1: 2}           entries paired off as a mapping
 *     ]           was  []               a bracket closing nothing at all
 *     k: [1] ]    was  ["k", [1]]       a stray bracket after a valid one
 *
 * Every one of those is a different document from the one written, produced
 * without a word of complaint - the same shape of fault as the flow-pair key
 * theft in "[a, : 1]": not a refusal that should have been an acceptance,
 * but an acceptance that quietly changed the meaning. Both references refuse
 * all of them.
 *
 * A single-pair level is not a flow mapping either. It is opened by a ":" or
 * a "?" inside a flow sequence and closed by whatever ends that entry, so a
 * "}" is never what ends it: "[a: 1}" is mismatched however the pair is read.
 */
#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* A closing bracket with nothing open. */
	{"]\n", nullptr},
	{"}\n", nullptr},
	{"k: ]\n", nullptr},
	{"k: }\n", nullptr},
	{"a: 1\n]\n", nullptr},
	{"a: 1\n}\n", nullptr},
	{"- ]\n", nullptr},
	{"- }\n", nullptr},

	/* A second closing bracket after the collection has already closed. */
	{"k: [1] ]\n", nullptr},
	{"k: {a: 1} }\n", nullptr},
	{"[1]]\n", nullptr},
	{"{a: 1}}\n", nullptr},
	{"[1] ]\n", nullptr},
	{"{a: 1} }\n", nullptr},

	/* The wrong kind of closing bracket. */
	{"{a: 1]\n", nullptr},
	{"[1, 2}\n", nullptr},
	{"{a: 1, b: 2]\n", nullptr},
	{"[a: 1}\n", nullptr},
	{"[ ? a : 1 }\n", nullptr},

	/* An opener with no closer stays an error too, which is the rule this
	 * one is the mirror of. */
	{"[\n", nullptr},
	{"{\n", nullptr},
	{"[1\n", nullptr},
	{"{a\n", nullptr},

	/* Nothing above touches a collection that is properly closed. */
	{"[]\n", "[]"},
	{"{}\n", "{}"},
	{"[1, 2]\n", "[1, 2]"},
	{"{a: 1}\n", "{\"a\": 1}"},
	{"[[1]]\n", "[[1]]"},
	{"{a: {b: 1}}\n", "{\"a\": {\"b\": 1}}"},
	{"[{a: 1}]\n", "[{\"a\": 1}]"},
	{"{a: [1]}\n", "{\"a\": [1]}"},
	{"[a: 1]\n", "[{\"a\": 1}]"},
	{"[ ? a : 1 ]\n", "[{\"a\": 1}]"},
	{"- [1]\n- {a: 1}\n", "[[1], {\"a\": 1}]"},
	{"k:\n  - [1]\n", "{\"k\": [[1]]}"},
};

} // namespace

TEST(YamlBracketMatching, EveryCloserNeedsItsOpener) {
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

/* Which bracket is wrong matters as much as that one is.
 *
 * A "}" against a single-pair level is refused either way - the "[" is left
 * open and the document ends inside it - so what the check buys is the
 * report. "Unterminated flow collection" sends a reader to the end of the
 * document looking for a missing bracket; the "}" they actually typed is the
 * thing that is wrong, and it is on the line the error names. */
TEST(YamlBracketMatching, TheReportNamesTheBracketThatIsWrong) {
	const char *inputs[] = {"[a: 1}\n", "[ ? a : 1 }\n", "[1, 2}\n"};
	for (const char *input : inputs) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), nullptr, &err);
		ASSERT_EQ(doc, nullptr)
			<< "input: " << ::testing::PrintToString(std::string(input));
		ASSERT_NE(err.message, nullptr);
		EXPECT_STREQ(err.message, "Unexpected } without matching {")
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
