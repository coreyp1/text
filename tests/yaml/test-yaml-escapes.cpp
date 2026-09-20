/**
 * The escapes a double-quoted scalar may carry (5.7).
 *
 * The list is closed: anything not on it is malformed, not a literal. The
 * scanner copied an unrecognised escape through as the character itself, so
 * '"\\."' came back as "." where every other parser refuses it. Four escapes
 * that ARE on the list were missing at the same time and were reaching that
 * same fallback, which gave the right answer for "\\/" and "\\ " and the
 * wrong one for the four named non-ASCII ones.
 *
 * "\\0" is left out of the table because the result holds a NUL, which a C
 * string expectation cannot carry; the scanner has handled it all along.
 *
 * Render() writes a line feed as \\n, a tab as \\t, a quote as \\" and a
 * backslash as \\\\, and every other byte as itself, so the expectations
 * below carry UTF-8 directly.
 *
 * Expectations are js-yaml's; PyYAML agrees on every case here.
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
	{"a: \"\\t\"\n", "{\"a\": \"\\t\"}"},  /* tab */
	{"a: \"\\n\"\n", "{\"a\": \"\\n\"}"},  /* line feed */
	{"a: \"\\r\"\n", "{\"a\": \"\x0d" "\"}"},  /* carriage return */
	{"a: \"\\\\\"\n", "{\"a\": \"\\\\\"}"},  /* a backslash */
	{"a: \"\\\"\"\n", "{\"a\": \"\\\"\"}"},  /* a quote */
	{"a: \"\\/\"\n", "{\"a\": \"/\"}"},  /* a solidus, for JSON compatibility */
	{"a: \"\\ \"\n", "{\"a\": \" \"}"},  /* an escaped space */
	{"a: \"x\\\ty\"\n", "{\"a\": \"x\\ty\"}"},  /* an escaped tab */
	{"a: \"\\x41\"\n", "{\"a\": \"A\"}"},  /* a hex escape */
	{"a: \"\\u0041\"\n", "{\"a\": \"A\"}"},  /* a 16-bit escape */
	{"a: \"\\U00000041\"\n", "{\"a\": \"A\"}"},  /* a 32-bit escape */
	{"a: \"\\N\"\n", "{\"a\": \"\xc2\x85" "\"}"},  /* next line */
	{"a: \"\\_\"\n", "{\"a\": \"\xc2\xa0" "\"}"},  /* non-breaking space */
	{"a: \"\\L\"\n", "{\"a\": \"\xe2\x80\xa8" "\"}"},  /* line separator */
	{"a: \"\\P\"\n", "{\"a\": \"\xe2\x80\xa9" "\"}"},  /* paragraph separator */
	{"a: \"\\e\"\n", "{\"a\": \"\x1b" "\"}"},  /* escape */
	{"a: \"\\a\"\n", "{\"a\": \"\x07" "\"}"},  /* bell */
};

const char *const kRefused[] = {
	"a: \"\\.\"\n",  /* not on the list */
	"a: \"\\q\"\n",  /* nor is this */
	"a: \"\\y\"\n",  /* nor this */
	"double: \"quoted \\' scalar\"\n",  /* suite case HRE5: a single quote is not escaped here */
};

const Case kSingleQuoted[] = {
	{"a: 'x\\.y'\n", "{\"a\": \"x\\\\.y\"}"},  /* a backslash in single quotes is an ordinary character */
	{"a: 'it''s'\n", "{\"a\": \"it's\"}"},  /* only the doubled quote means anything */
};

}  // namespace

TEST(YamlEscapes, EveryEscapeOnTheList) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

TEST(YamlEscapes, AndNothingElse) {
	for (const char *input : kRefused) {
		EXPECT_EQ(Render(input), std::string(""))
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

TEST(YamlEscapes, SingleQuotesHaveNoneAtAll) {
	for (const Case &c : kSingleQuoted) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
