/**
 * Flow folding inside quoted scalars (6.5, 7.3.1, 7.3.2).
 *
 * Plain and block scalars folded their line breaks; single- and
 * double-quoted ones did not. They returned the break verbatim along with
 * the indentation that opened the next line, so a wrapped message came back
 * with the wrapping in it - "a\n  b" where every other implementation says
 * "a b". This was the largest single cluster in yaml-test-suite, 32 cases.
 *
 * The rules, in the order this file exercises them: the white space before a
 * break is not content, one break folds to a space, a run of n breaks folds
 * to n-1 line feeds, and the indentation opening each continuation line is
 * not content. Double quotes add one more: a backslash immediately before a
 * break removes the break entirely, and there the white space in front of
 * the backslash *is* content - spec example 7.5 keeps a tab there.
 *
 * Expectations are PyYAML's. js-yaml disagrees on three of them, all the
 * same mistake - it keeps the white space that precedes a break - and spec
 * example 7.5 (suite case NP9H) settles those in PyYAML's favour.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
}

namespace {

std::string g_last_scalar;

GTEXT_YAML_Status CaptureLastScalar(GTEXT_YAML_Stream *s, const void *payload,
                                    void *user) {
	(void)s;
	(void)user;
	const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)payload;
	if (e->type == GTEXT_YAML_EVENT_SCALAR) {
		g_last_scalar.assign(e->data.scalar.ptr, e->data.scalar.len);
	}
	return GTEXT_YAML_OK;
}



struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	{"k: \"a\nb\"\n", "{\"k\": \"a b\"}"},  /* one break folds to a space */
	{"k: \"a\n\nb\"\n", "{\"k\": \"a\\nb\"}"},  /* two breaks fold to one line feed */
	{"k: \"a\n\n\nb\"\n", "{\"k\": \"a\\n\\nb\"}"},  /* three breaks fold to two line feeds */
	{"k: \"a\n   b\"\n", "{\"k\": \"a b\"}"},  /* the indentation opening the next line is not content */
	{"k: \"a   \n   b\"\n", "{\"k\": \"a b\"}"},  /* nor is the white space before the break */
	{"k: \"a\t\nb\"\n", "{\"k\": \"a b\"}"},  /* a tab before the break goes the same way */
	{"k: \"a\n   \n   b\"\n", "{\"k\": \"a\\nb\"}"},  /* a line of only white space is an empty line */
	{"k: \"a\n\"\n", "{\"k\": \"a \"}"},  /* a break with nothing after it still folds */
	{"k: 'a\nb'\n", "{\"k\": \"a b\"}"},  /* single quotes fold identically */
	{"k: 'a\n\nb'\n", "{\"k\": \"a\\nb\"}"},  /* single quotes, two breaks */
	{"k: 'a   \n   b'\n", "{\"k\": \"a b\"}"},  /* single quotes drop the white space too */
	{"k: 'a''b\nc'\n", "{\"k\": \"a'b c\"}"},  /* an escaped quote is content, so the run of white space restarts after it */
	{"k: \"a\\\nb\"\n", "{\"k\": \"ab\"}"},  /* a backslash escapes the break away entirely */
	{"k: \"a  \\\nb\"\n", "{\"k\": \"a  b\"}"},  /* white space before an escaped break is content (spec example 7.5) */
	{"k: \"a\\\n\nb\"\n", "{\"k\": \"a\\nb\"}"},  /* an empty line after an escaped break still folds to a line feed */
	{"k: \"a\\tb\nc\"\n", "{\"k\": \"a\\tb c\"}"},  /* a tab written as an escape is content */
	{"k: \"a\\t\nb\"\n", "{\"k\": \"a\\t b\"}"},  /* so an escaped tab survives a break the literal tab would not */
	{"k: \"\n  a\"\n", "{\"k\": \" a\"}"},  /* a break at the very start of the scalar */
	{"k: \"a\r\nb\"\n", "{\"k\": \"a b\"}"},  /* CRLF is one break */
	{"k: \"a\rb\"\n", "{\"k\": \"a b\"}"},  /* a lone CR is one break */
	{"k: \"a\n\r\nb\"\n", "{\"k\": \"a\\nb\"}"},  /* CR and CRLF mixed count as two breaks */
	{"k: \"a b\"\n", "{\"k\": \"a b\"}"},  /* no break, nothing to fold */
	{"k: \"a \tb\"\n", "{\"k\": \"a \\tb\"}"},  /* interior white space is content */
	{"k: \"a\n b\n c\"\n", "{\"k\": \"a b c\"}"},  /* three lines fold to two spaces */
	{"k: \"a\n\nb\n\nc\"\n", "{\"k\": \"a\\nb\\nc\"}"},  /* alternating breaks */
};

}  // namespace

TEST(YamlQuotedFolding, FoldsLineBreaks) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* Spec example 7.5, which is where the escaped-break rule comes from: the
   white space before a normal break is dropped and the white space before an
   escaped one is kept, in the same document. */
TEST(YamlQuotedFolding, SpecExample75) {
	const char *input =
		"\"folded \n"
		"to a space,\t\n"
		" \n"
		"to a line feed, or \t\\\n"
		" \\ \tnon-content\"\n";
	EXPECT_EQ(Render(input),
		std::string("\"folded to a space,\\nto a line feed, or \\t \\tnon-content\""));
}

/* Folding must not depend on how the input is cut up. The scanner decides a
   fold by looking past the break for the line that follows it, and until
   that line has arrived it has to report E_INCOMPLETE rather than guess:
   scanner_consume() may compact the buffer, so bytes it has taken are gone
   and there is nothing to rewind to. Feeding one byte at a time is the
   harshest version of that, and it has to give the same answer as one feed. */
TEST(YamlQuotedFolding, SurvivesBeingFedOneByteAtATime) {
	static const char *const kInputs[] = {
		"k: \"a\n\n  b\"\n",
		"k: \"a   \n   b\"\n",
		"k: \"a\\\n  b\"\n",
		"k: 'a\n\n  b'\n",
	};
	static const char *const kWants[] = { "a\nb", "a b", "ab", "a\nb" };

	for (size_t i = 0; i < sizeof(kInputs) / sizeof(kInputs[0]); ++i) {
		const char *input = kInputs[i];
		const size_t len = strlen(input);
		GTEXT_YAML_Stream *stream =
			gtext_yaml_stream_new(nullptr, CaptureLastScalar, nullptr);
		ASSERT_NE(stream, nullptr);
		g_last_scalar.clear();
		for (size_t j = 0; j < len; ++j) {
			ASSERT_EQ(gtext_yaml_stream_feed(stream, input + j, 1), GTEXT_YAML_OK)
				<< "input " << i << " byte " << j;
		}
		EXPECT_EQ(gtext_yaml_stream_finish(stream), GTEXT_YAML_OK);
		gtext_yaml_stream_free(stream);
		EXPECT_EQ(g_last_scalar, std::string(kWants[i]))
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
