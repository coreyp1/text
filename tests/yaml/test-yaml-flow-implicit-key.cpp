/**
 * Where a flow entry's key and its ":" may sit relative to each other.
 *
 * A single-pair entry of a flow *sequence* keeps both on one line. "[a: 1]"
 * is ns-flow-pair, and its key is ns-s-implicit-yaml-key(c): a node followed
 * by s-separate-in-line?, which is white space with no break in it (7.4). So
 *
 *     [ key
 *       : value ]
 *
 * has no production; it parsed as {"key": "value"} (suite cases DK4H, ZXT5).
 *
 * A flow *mapping* is the opposite case. ns-flow-map-yaml-key-entry reaches
 * its ":" across s-separate(n,c), which may hold a line break, so
 *
 *     {"foo"
 *     : "bar"}
 *
 * is well formed (4MUZ), and so are a key that itself spans lines (9SA2,
 * NJ66), a comment between the key and the colon (K3WX), and every part on
 * its own line (VJP3). The first draft of this rule refused all of them -
 * seven cases - by treating both flow collections the same. They are the
 * reason those rows are here.
 *
 * An explicit key is exempt in either: "?" is what spans lines.
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
	/* A flow sequence's single-pair entry: one line, or nothing. */
	{"[ key\n  : value ]\n", nullptr},
	{"[ \"key\"\n  :value ]\n", nullptr},
	{"[ a\n  : 1, b: 2 ]\n", nullptr},
	{"[ key: value ]\n", "[{\"key\": \"value\"}]"},
	{"[ \"key\": value ]\n", "[{\"key\": \"value\"}]"},
	{"[ a: 1,\n  b: 2 ]\n", "[{\"a\": 1}, {\"b\": 2}]"},

	/* A flow mapping may put a break between the key and the ":". */
	{"{\"foo\"\n: \"bar\"}\n", "{\"foo\": \"bar\"}"},
	{"{\"foo\"\n: bar}\n", "{\"foo\": \"bar\"}"},
	{"{ \"foo\"\n  :bar }\n", "{\"foo\": \"bar\"}"},
	{"{ \"foo\" # comment\n  :bar }\n", "{\"foo\": \"bar\"}"},
	{"k: {\n  k\n  :\n  v\n  }\n", "{\"k\": {\"k\": \"v\"}}"},
	{"{ single line: value}\n", "{\"single line\": \"value\"}"},
	{"{ multi\n  line: value}\n", "{\"multi line\": \"value\"}"},
	{"{ \"multi\n  line\": value}\n", "{\"multi line\": \"value\"}"},

	/* A flow mapping nested in a flow sequence is still a flow mapping. */
	{"[ {\"foo\"\n: \"bar\"} ]\n", "[{\"foo\": \"bar\"}]"},

	/* An explicit key spans lines wherever it appears - in a flow mapping,
	   which is the case this rule had to leave alone. The flow *sequence*
	   spelling, "[ ? key : value ]", is refused for an unrelated reason that
	   predates this rule and is not fixed here; both references accept it. */
	{"{ ? key\n  : value }\n", "{\"key\": \"value\"}"},
};

} // namespace

TEST(YamlFlowImplicitKey, SequencePairsKeepTheirColonOnOneLine) {
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
