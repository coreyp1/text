/**
 * An anchor on a block collection.
 *
 * An anchor applies to the node that follows it, and in block context
 * nothing says whether that node is the next scalar or a collection whose
 * first key or item it is until the parser has seen what comes next. A flow
 * collection has a "[" or "{" for the anchor to arrive with; a block one has
 * no such token, so the anchor stayed on whichever scalar the stream could
 * attach it to - the first entry of a sequence, or the first key of a
 * mapping.
 *
 * Nothing reported an error. The alias simply resolved to the wrong node:
 *
 *     bill-to: &id001      *id001 gave the string "given" rather than the
 *         given  : Chris   mapping (spec example 2.27, suite case UGM3).
 *         family : Dumars
 *     ship-to: *id001
 *
 *     x: &anc              *anc gave 1 rather than [1].
 *       - 1
 *     y: *anc
 *
 * The tag half of this was fixed before - see the deviations section of
 * documentation/formats/yaml.md - and adopt_own_line_tag() is what does it.
 * The anchor half was never done, and is adopt_own_line_anchor() now.
 *
 * Only an anchor written on an earlier line moves. "&a key: 1" anchors the
 * key, exactly as "!custom key: 1" tags it; the two spellings differ in
 * nothing but where the property was written, which is why the event carries
 * anchor_line beside tag_line.
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
	/* The alias is what shows where the anchor landed. */
	{"x: &anc\n  - 1\ny: *anc\n", "{\"x\": [1], \"y\": [1]}"},
	{"x: &anc\n  a: 1\ny: *anc\n", "{\"x\": {\"a\": 1}, \"y\": {\"a\": 1}}"},
	{"x: &anc\n  - 1\n  - 2\ny: *anc\n", "{\"x\": [1, 2], \"y\": [1, 2]}"},

	/* Spec example 2.27's shape, which is what found this. */
	{"bill-to: &id001\n    given  : Chris\n    family : Dumars\nship-to: *id001\n",
	 "{\"bill-to\": {\"given\": \"Chris\", \"family\": \"Dumars\"}, "
	 "\"ship-to\": {\"given\": \"Chris\", \"family\": \"Dumars\"}}"},

	/* A flow collection always worked: the anchor arrives with the "[". */
	{"x: &anc [1]\ny: *anc\n", "{\"x\": [1], \"y\": [1]}"},
	{"x: &anc {a: 1}\ny: *anc\n", "{\"x\": {\"a\": 1}, \"y\": {\"a\": 1}}"},

	/* A scalar keeps its own anchor. */
	{"x: &anc 1\ny: *anc\n", "{\"x\": 1, \"y\": 1}"},
	{"x: &anc\ny: *anc\n", "{\"x\": null, \"y\": null}"},

	/* An anchor on the key's own line belongs to the key, not the mapping,
	   which is the same split the tag rule makes. */
	{"x:\n  &k a: 1\ny: *k\n", "{\"x\": {\"a\": 1}, \"y\": \"a\"}"},

	/* At the root there is no enclosing collection to adopt it. The scalar
	   case is the one that reaches the rule at stack depth zero, where there
	   is no enclosing level to read at all. */
	{"&anc\n- 1\n", "[1]"},
	{"&anc\nfoo\n", "\"foo\""},

	/* An explicit key may be a collection rather than a scalar, and the
	   anchor it carries is not in the same place in the node. */
	{"? &k [1]\n: 2\n", "{[1]: 2}"},
	{"? &k\n  [1]\n: 2\n", "{[1]: 2}"},

	/* A flow collection that already has an anchor of its own keeps it: the
	   one written inside it belongs to the entry, not to the collection. */
	{"x: &outer [\n  &inner\n  1 ]\ny: *outer\n",
	 "{\"x\": [1], \"y\": [1]}"},
};

} // namespace

TEST(YamlCollectionAnchor, ReachesTheCollectionNotItsFirstScalar) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* The anchor has to be on the collection node itself, not merely resolvable
   through it: gtext_yaml_node_anchor() is what a caller walking the DOM
   sees, and it was empty. */
TEST(YamlCollectionAnchor, TheCollectionNodeCarriesTheAnchor) {
	const char *input = "x: &anc\n  a: 1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *x = gtext_yaml_mapping_get(root, "x");
	ASSERT_NE(x, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(x), GTEXT_YAML_MAPPING);
	EXPECT_STREQ(gtext_yaml_node_anchor(x), "anc");
	/* And the key it used to sit on no longer claims it. */
	const GTEXT_YAML_Node *key = nullptr, *val = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(x, 0, &key, &val));
	EXPECT_EQ(gtext_yaml_node_anchor(key), nullptr);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
