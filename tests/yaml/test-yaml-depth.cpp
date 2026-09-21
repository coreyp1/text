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

/* And the scanner had a second limit nobody set: a fixed 32-entry array for
   the flow-context stack, whose push was *dropped* when it ran out while the
   matching pop still counted down. Past 32 nested flow collections the
   scanner believed it was back in block context with the brackets still
   open, and mis-scanned what followed.

   It does not fail on every shape - plain "[[[...a: 1...]]]" forty deep comes
   back right, because one dropped push and one clamped pop cancel - which is
   why it took a fuzzer and a particular shape to surface. The array grows
   now; max_depth is the limit, and it is the only one. */
TEST(YamlDepth, TheScannerHasNoLimitOfItsOwn) {
	/* The shape the writer fuzzer produced, reduced: two runs of nested flow
	   sequences either side of a mapping, inside a mapping with an empty key.
	   At 13 it parsed and at 14 it did not, because 33 brackets are open at
	   the deepest point. */
	for (size_t n : {13, 14, 20, 40}) {
		std::string in = "[{: [{" + std::string(n, '[') + "{\":\": "
			+ std::string(n, '[') + "~" + std::string(n, ']') + "}"
			+ std::string(n, ']') + ": }]}, ~]";
		EXPECT_EQ(ParseStatus(in, 256), GTEXT_YAML_OK) << "n = " << n;
	}

	/* And plain nesting past 32 still reads back as what it is. */
	for (size_t n : {32, 33, 64}) {
		const std::string in =
			std::string(n, '[') + "a: 1" + std::string(n, ']');
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		opts.max_depth = 256;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(in.data(), in.size(), &opts, &err);
		ASSERT_NE(doc, nullptr) << "n = " << n << ": "
			<< (err.message ? err.message : "");
		gtext_yaml_error_free(&err);
		const GTEXT_YAML_Node *node = gtext_yaml_document_root(doc);
		size_t seen = 0;
		while (node && gtext_yaml_node_type(node) == GTEXT_YAML_SEQUENCE) {
			ASSERT_EQ(gtext_yaml_sequence_length(node), (size_t)1) << "n = " << n;
			node = gtext_yaml_sequence_get(node, 0);
			seen++;
		}
		EXPECT_EQ(seen, n) << "n = " << n;
		ASSERT_NE(node, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(node), GTEXT_YAML_MAPPING) << "n = " << n;
		gtext_yaml_free(doc);
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
