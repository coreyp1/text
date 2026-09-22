/**
 * Four writer behaviours that 37,906 corpus files could not distinguish.
 *
 * These were found while converting the writer from recursive descent to an
 * explicit stack. To prove the conversion changed no output, a harness parsed
 * every file in the fuzz corpus, wrote each under 180 combinations of schema,
 * comment retention, flow style, pretty, scalar style, line width and indent,
 * and hashed the result - 970,740 writes, byte-identical before and after.
 *
 * Then the harness was mutation-tested, and five deliberate changes to the
 * writer moved **nothing** across all of it. Twelve hand-written documents
 * caught four of them at once. The fifth needed a reading of
 * collection_is_flow(), which returns true for every descendant of a flow
 * collection: so a nested flow collection restores in_flow from true to true,
 * and that restore is observable only at the document root.
 *
 * A corpus measures what is reachable from its seeds, not what the code does.
 * These are the probes, kept, so the paths stop depending on someone
 * rediscovering them.
 */
#include <gtest/gtest.h>
#include <string.h>

#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/* Parse with comments retained, write with the given options, return output. */
std::string RoundTripText(
		const std::string &src, const GTEXT_YAML_Write_Options *wopts) {
	GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
	popts.retain_comments = true;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(src.data(), src.size(), &popts, &err);
	EXPECT_NE(doc, nullptr) << (err.message ? err.message : "?") << " for " << src;
	gtext_yaml_error_free(&err);
	if (!doc) return "";

	GTEXT_YAML_Sink sink;
	EXPECT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options defaults = gtext_yaml_write_options_default();
	EXPECT_EQ(
		gtext_yaml_write_document(doc, &sink, wopts ? wopts : &defaults),
		GTEXT_YAML_OK);
	std::string out(
		gtext_yaml_sink_buffer_data(&sink), gtext_yaml_sink_buffer_size(&sink));
	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
	return out;
}

/* Whether the output still parses, which is the point of all of this. */
bool Reparses(const std::string &text) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(text.data(), text.size(), nullptr, &err);
	bool ok = doc != nullptr;
	if (doc) gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
	return ok;
}

}  // namespace

/* The control. If comments are not being retained none of the rest of this
   file is measuring anything - three of the four behaviours below exist only
   because a node carries a comment. */
TEST(YamlWriterFlowContext, CommentsSurviveIntoTheOutput) {
	const std::string out = RoundTripText("[1, 2] # root comment\n", nullptr);
	EXPECT_NE(out.find("# root comment"), std::string::npos)
		<< "no comment in the output at all: [" << out << "]";
}

/* A flow collection raises in_flow for its children and puts it back before
   its own trailing comment is written, because that comment belongs to
   whatever holds the collection. At the root there is no holder, so the
   comment ends the document - and leaving in_flow raised appends a line
   break and an indent to a finished document.
   
   Only visible at the root: collection_is_flow() returns true for every
   descendant of a flow collection, so a nested one restores true to true. */
TEST(YamlWriterFlowContext, ARootFlowCollectionEndsWhereItsCommentEnds) {
	for (const char *src : {"[1, 2] # root comment\n", "{a: 1} # root map\n"}) {
		const std::string out = RoundTripText(src, nullptr);
		ASSERT_FALSE(out.empty()) << src;
		const size_t last = out.find_last_not_of(" \t\r\n");
		EXPECT_EQ(last, out.size() - 1)
			<< "trailing white space after the root comment: [" << out << "]";
		EXPECT_TRUE(Reparses(out)) << out;
	}
}

/* A key carrying an inline comment cannot be an implicit key: the comment
   runs to the end of the line (7.1) and an implicit key has to share its line
   with the ":" after it. Written implicitly it comes back as a comment and no
   entry at all - a mapping of one going out and a mapping of none coming
   back, with nothing to say so. */
TEST(YamlWriterFlowContext, AKeyWithACommentTakesTheExplicitForm) {
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	wopts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;

	const std::string out = RoundTripText("? k # note\n: v\nother: 1\n", &wopts);
	ASSERT_FALSE(out.empty());
	EXPECT_NE(out.find("? "), std::string::npos)
		<< "the key went out implicitly, so its comment eats the colon: ["
		<< out << "]";
	ASSERT_TRUE(Reparses(out)) << out;

	/* And it is still a mapping of two when it comes back. */
	GTEXT_YAML_Document *doc = gtext_yaml_parse(out.data(), out.size(), nullptr, nullptr);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(gtext_yaml_document_root(doc)), 2u)
		<< out;
	gtext_yaml_free(doc);
}

/* An alias sets key_absorbs_colon, because "*n:" would scan as an anchor name
   ending in a colon rather than an alias followed by one. The writer has to
   put a space in. */
TEST(YamlWriterFlowContext, AnAliasKeyKeepsItsColonSeparate) {
	const std::string out =
		RoundTripText("anchor: &n value\nflow: {*n : 1, k: 2}\n", nullptr);
	ASSERT_FALSE(out.empty());
	EXPECT_NE(out.find("*n :"), std::string::npos)
		<< "the colon was written hard against the alias: [" << out << "]";
	EXPECT_TRUE(Reparses(out)) << out;
}

/* A comment on a collection is the collection's, and has to be written after
   its closing bracket rather than dropped. */
TEST(YamlWriterFlowContext, AFlowCollectionKeepsItsOwnComment) {
	const std::string out =
		RoundTripText("a: [1, 2] # after the bracket\nb: 3\n", nullptr);
	ASSERT_FALSE(out.empty());
	EXPECT_NE(out.find("# after the bracket"), std::string::npos)
		<< "the collection's comment was dropped: [" << out << "]";
	EXPECT_TRUE(Reparses(out)) << out;
}
