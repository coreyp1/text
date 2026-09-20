/**
 * Document markers inside a multi-line scalar.
 *
 * c-forbidden is "---" or "..." at the start of a line followed by white
 * space, a break, or end of input (9.1.2), and a document's content may not
 * contain one. The marker ends the document wherever it appears, so a scalar
 * spanning it never reaches its closing quote.
 *
 * The scanner folded straight over them:
 *
 *     ---        the "---" is a document-start marker, so the double-quoted
 *     "          scalar opened on line 2 is never closed - but this came
 *     ---        back as the string " --- " (suite case 5TRB, and RXY3 is
 *     "          the same shape with "..." and single quotes).
 *
 * The three characters have to be followed by white space to count, which is
 * what separates the two halves of suite case 9MQT: "...x" on its own line is
 * ordinary content and parses, "... x" is forbidden and does not. Indentation
 * disqualifies a marker too - it is a production of column 1 - so an indented
 * "..." inside a block scalar is content, which is how block scalars are
 * unaffected by any of this.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* A marker at column 1 inside a quoted scalar. */
	{"---\n\"\n---\n\"\n", nullptr},
	{"---\n'\n...\n'\n", nullptr},
	{"--- \"a\n... x\nb\"\n", nullptr},
	{"--- \"a\n---\nb\"\n", nullptr},
	{"--- 'a\n...\nb'\n", nullptr},
	{"--- \"a\n...\nb\"\n", nullptr},
	/* And in a plain one. */
	{"--- a\n... x\nb\n", nullptr},

	/* Not followed by white space, so not a marker. */
	{"--- \"a\n...x\nb\"\n", "\"a ...x b\""},
	{"--- \"a\n---x\nb\"\n", "\"a ---x b\""},
	/* Wrong length: it takes exactly three of the same character. */
	{"--- \"a\n..\nb\"\n", "\"a .. b\""},
	{"--- \"a\n....\nb\"\n", "\"a .... b\""},
	{"--- \"a\n- x\nb\"\n", "\"a - x b\""},
	{"--- \"a\n. x\nb\"\n", "\"a . x b\""},
	{"--- \"a\n-- x\nb\"\n", "\"a -- x b\""},
	/* Indented, so not at the start of a line. */
	{"--- \"a\n ...\nb\"\n", "\"a ... b\""},
	{"--- |\n  ...\n", "\"...\\n\""},
	{"--- |\n  ---\n", "\"---\\n\""},

	/* An escaped break folds through the same code, so the check has to be
	   made there too. */
	{"--- \"a\\\n---\nb\"\n", nullptr},
	{"--- \"a\\\n...x\nb\"\n", "\"a...x b\""},
};

} // namespace

TEST(YamlForbiddenMarker, MayNotAppearInsideAMultiLineScalar) {
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

/* Telling a marker from content needs the three characters and the one after
   them, so the scanner has to ask for more input rather than guess when they
   have not all arrived. scanner_consume() may compact the buffer, which is
   why guessing cannot be undone. Feeding one byte at a time is the harshest
   version of that and has to give the same answer as one feed. */
TEST(YamlForbiddenMarker, SurvivesBeingFedOneByteAtATime) {
	static const char *const kInputs[] = {
		"--- \"a\n...x\nb\"\n",   /* content */
		"--- \"a\n... x\nb\"\n",  /* forbidden */
		"--- \"a\n..\nb\"\n",     /* content */
		"--- \"a\n---\nb\"\n",    /* forbidden */
	};
	static const bool kRefused[] = { false, true, false, true };

	for (size_t i = 0; i < sizeof(kInputs) / sizeof(kInputs[0]); ++i) {
		const char *input = kInputs[i];
		const size_t len = strlen(input);

		GTEXT_YAML_Error whole_err;
		memset(&whole_err, 0, sizeof(whole_err));
		GTEXT_YAML_Document *whole =
			gtext_yaml_parse(input, len, nullptr, &whole_err);
		EXPECT_EQ(whole == nullptr, kRefused[i]) << "one feed, input " << i;

		/* One byte at a time has to reach the same verdict. */
		GTEXT_YAML_Status st = GTEXT_YAML_OK;
		GTEXT_YAML_Stream *stream =
			gtext_yaml_stream_new(nullptr, nullptr, nullptr);
		ASSERT_NE(stream, nullptr);
		for (size_t j = 0; j < len && st == GTEXT_YAML_OK; ++j) {
			st = gtext_yaml_stream_feed(stream, input + j, 1);
		}
		if (st == GTEXT_YAML_OK) st = gtext_yaml_stream_finish(stream);
		EXPECT_EQ(st != GTEXT_YAML_OK, kRefused[i])
			<< "byte at a time, input " << i;
		gtext_yaml_stream_free(stream);

		if (whole) gtext_yaml_free(whole);
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
