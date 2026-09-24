/**
 * @file test-yaml-comment-roundtrip.cpp
 * @brief Parse a commented, styled document and write it back.
 *
 * test-yaml-comments.cpp covers the two halves separately: that a parse
 * retains comments, and that a writer emits comments a caller *set*. What was
 * missing is the case an adopter actually has - read a configuration file,
 * change one value, write it out - and the documentation said it did not work,
 * which measurement contradicted. So these tests pin the whole cycle, and pin
 * the one real limitation as well: comments and block scalars are properties of
 * block style, and the default write options are flow.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <string>
#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

// Parse with comments retained, then write, and return the output.
std::string cycle(const std::string & input, bool pretty, bool * ok) {
	*ok = false;
	GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
	popts.retain_comments = true;
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document * doc =
	    gtext_yaml_parse(input.data(), input.size(), &popts, &error);
	if (!doc) {
		return std::string();
	}
	GTEXT_YAML_Sink sink;
	if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) {
		gtext_yaml_free(doc);
		return std::string();
	}
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	wopts.pretty = pretty;
	std::string out;
	if (gtext_yaml_write_document(doc, &sink, &wopts) == GTEXT_YAML_OK) {
		const char * data = gtext_yaml_sink_buffer_data(&sink);
		if (data) {
			out.assign(data, gtext_yaml_sink_buffer_size(&sink));
			*ok = true;
		}
	}
	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
	return out;
}

} // namespace

/* The case the documentation denied: comments that came from a parse are
   written back out. Leading and inline, on a mapping key, on a nested key, and
   on a sequence entry. */
TEST(YamlCommentRoundTrip, CommentsFromAParseAreWrittenBack) {
	const std::string input =
	    "# about the document\n"
	    "key: value # about the value\n"
	    "list:\n"
	    "  # about the first entry\n"
	    "  - one\n"
	    "  - two # about the second\n"
	    "nested:\n"
	    "  # about the inner key\n"
	    "  inner: 1\n";

	bool ok = false;
	const std::string out = cycle(input, true, &ok);
	ASSERT_TRUE(ok) << "the cycle failed";

	for (const char * fragment : {
	         "# about the document",
	         "value # about the value",
	         "# about the first entry",
	         "two # about the second",
	         "# about the inner key",
	     }) {
		EXPECT_NE(out.find(fragment), std::string::npos)
		    << "missing [" << fragment << "] from:\n"
		    << out;
	}
}

/* And the comments survive being read back, so the cycle can be repeated - a
   tool that rewrites a file does not lose a comment on the second run. */
TEST(YamlCommentRoundTrip, TheCycleIsRepeatable) {
	const std::string input =
	    "# lead\n"
	    "key: value # inline\n"
	    "block: |\n"
	    "  text\n";

	bool ok = false;
	const std::string once = cycle(input, true, &ok);
	ASSERT_TRUE(ok);
	const std::string twice = cycle(once, true, &ok);
	ASSERT_TRUE(ok);
	EXPECT_EQ(once, twice) << "--- once ---\n" << once << "--- twice ---\n"
	                       << twice;
	EXPECT_NE(once.find("# lead"), std::string::npos) << once;
	EXPECT_NE(once.find("value # inline"), std::string::npos) << once;
}

/* Scalar style survives a parse-write cycle in block mode, which the
   documentation also denied. Each of the five styles, including the two block
   ones, which cannot be written at all in flow context. */
TEST(YamlCommentRoundTrip, ScalarStyleSurvivesTheCycle) {
	const std::string input =
	    "plain: bare words\n"
	    "double: \"in double quotes\"\n"
	    "single: 'in single quotes'\n"
	    "folded: >\n"
	    "  folded text\n"
	    "literal: |\n"
	    "  literal text\n";

	bool ok = false;
	const std::string out = cycle(input, true, &ok);
	ASSERT_TRUE(ok);

	EXPECT_NE(out.find("plain: bare words"), std::string::npos) << out;
	EXPECT_NE(out.find("double: \"in double quotes\""), std::string::npos)
	    << out;
	EXPECT_NE(out.find("single: 'in single quotes'"), std::string::npos) << out;
	EXPECT_NE(out.find("folded: >"), std::string::npos) << out;
	EXPECT_NE(out.find("literal: |"), std::string::npos) << out;
}

/* The limitation, pinned so it is a decision rather than a surprise: the
   default write options are flow style, and a flow collection cannot carry a
   block scalar - a literal or folded scalar has to go out quoted there - nor a
   comment on a line of its own. The inline comment does survive, because a
   comment runs to the end of a line and the writer breaks the line for it.
   Anyone rewriting a file wants pretty = true. */
TEST(YamlCommentRoundTrip, FlowStyleIsTheOneThatLoses) {
	const std::string input =
	    "# leading\n"
	    "key: value # inline\n"
	    "literal: |\n"
	    "  text\n";

	bool ok = false;
	const std::string flow = cycle(input, false, &ok);
	ASSERT_TRUE(ok);

	// Flow style: the document is one braced collection.
	EXPECT_EQ(flow.find('{'), 0u) << flow;
	// The leading comment has nowhere to go.
	EXPECT_EQ(flow.find("# leading"), std::string::npos) << flow;
	// The inline one survives.
	EXPECT_NE(flow.find("# inline"), std::string::npos) << flow;
	// The block scalar cannot be spelled here, so it is quoted instead.
	EXPECT_EQ(flow.find("literal: |"), std::string::npos) << flow;
	EXPECT_NE(flow.find("\"text\\n\""), std::string::npos) << flow;

	// And block style keeps all three.
	const std::string block = cycle(input, true, &ok);
	ASSERT_TRUE(ok);
	EXPECT_NE(block.find("# leading"), std::string::npos) << block;
	EXPECT_NE(block.find("# inline"), std::string::npos) << block;
	EXPECT_NE(block.find("literal: |"), std::string::npos) << block;
}

/* retain_comments is off by default, and then there is nothing to write: the
   cycle above is doing what it says rather than the writer inventing text. */
TEST(YamlCommentRoundTrip, WithoutRetainCommentsThereIsNothingToWrite) {
	const std::string input = "# leading\nkey: value # inline\n";

	GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
	EXPECT_FALSE(popts.retain_comments);
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document * doc =
	    gtext_yaml_parse(input.data(), input.size(), &popts, &error);
	ASSERT_NE(doc, nullptr);

	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	wopts.pretty = true;
	ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &wopts), GTEXT_YAML_OK);
	const std::string out(gtext_yaml_sink_buffer_data(&sink),
	    gtext_yaml_sink_buffer_size(&sink));
	EXPECT_EQ(out.find('#'), std::string::npos) << out;
	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
}

int main(int argc, char ** argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
