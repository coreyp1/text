/**
 * max_depth, and the half of the document it was not reaching.
 *
 * The limit was counted in the stream layer, at the "[" and "{" of a flow
 * collection. Block structure never touched it, because block structure is
 * composed by the DOM parser rather than reported by the scanner - so
 *
 *     - - - - - ... x        (five thousand of them)
 *
 * parsed to a DOM five thousand deep with a limit of 256 in force. That is
 * exactly the input a nesting limit exists for, and it was the one input the
 * limit did not see.
 *
 * It surfaced through the writer: the flow-style writer turns block nesting
 * into flow nesting, which *is* counted, so the writer produced a document
 * this parser then refused. A limit that only some spellings of the same
 * document reach is not a limit.
 *
 * The old test here asked "if it failed, did it fail with E_DEPTH?", which
 * passes when nothing fails at all.
 */
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/* n nested block sequences: "- - - ... x". */
std::string BlockSequence(size_t n) {
	std::string s;
	for (size_t i = 0; i < n; ++i) s += "- ";
	s += "x";
	return s;
}

/* n nested block mappings, one indent step each. */
std::string BlockMapping(size_t n) {
	std::string s;
	for (size_t i = 0; i < n; ++i) s += std::string(i * 2, ' ') + "k:\n";
	return s;
}

/* n nested flow sequences. */
std::string FlowSequence(size_t n) {
	return std::string(n, '[') + "1" + std::string(n, ']');
}

GTEXT_YAML_Status ParseStatus(const std::string &in, size_t max_depth) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.max_depth = max_depth;
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(in.data(), in.size(), &opts, &err);
	if (doc) {
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
		return GTEXT_YAML_OK;
	}
	const GTEXT_YAML_Status st = err.code;
	gtext_yaml_error_free(&err);
	return st;
}

}  // namespace

TEST(YamlDepth, TheLimitReachesBlockCollections) {
	EXPECT_EQ(ParseStatus(BlockSequence(10), 64), GTEXT_YAML_OK);
	EXPECT_EQ(ParseStatus(BlockSequence(500), 64), GTEXT_YAML_E_DEPTH);
	EXPECT_EQ(ParseStatus(BlockSequence(5000), 256), GTEXT_YAML_E_DEPTH);

	EXPECT_EQ(ParseStatus(BlockMapping(10), 64), GTEXT_YAML_OK);
	EXPECT_EQ(ParseStatus(BlockMapping(500), 64), GTEXT_YAML_E_DEPTH);
}

TEST(YamlDepth, TheLimitStillReachesFlowCollections) {
	EXPECT_EQ(ParseStatus(FlowSequence(10), 64), GTEXT_YAML_OK);
	EXPECT_EQ(ParseStatus(FlowSequence(500), 64), GTEXT_YAML_E_DEPTH);
}

/* A depth refusal is not an allocation failure, and every caller of
   stack_push() used to report one. */
TEST(YamlDepth, ItIsReportedAsDepthAndNotAsMemory) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.max_depth = 4;
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	const std::string in = BlockSequence(64);
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(in.data(), in.size(), &opts, &err);
	ASSERT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_DEPTH);
	EXPECT_STREQ(err.message, "Maximum nesting depth exceeded");
	gtext_yaml_error_free(&err);
}

/* Zero means no limit, which is what the field's documentation says. */
TEST(YamlDepth, ZeroMeansNoLimit) {
	EXPECT_EQ(ParseStatus(BlockSequence(2000), 0), GTEXT_YAML_OK);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
