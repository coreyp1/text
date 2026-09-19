#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlStandardTags, TimestampValidDate) {
	const char *yaml = "!!timestamp 2025-02-14";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, TimestampValidDateTime) {
	const char *yaml = "!!timestamp 2025-02-14T10:30:45Z";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(root), "2025-02-14T10:30:45Z");
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, TimestampInvalid) {
	const char *yaml = "!!timestamp 2025-13-40";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlStandardTags, TimestampNormalizesOffset) {
	const char *yaml = "!!timestamp 2025-02-14 10:30+02";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(root), "2025-02-14T10:30:00+02:00");
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, TimestampNormalizesFraction) {
	const char *yaml = "!!timestamp 2025-02-14T10:30:45.5000Z";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(root), "2025-02-14T10:30:45.5Z");
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, SetValid) {
	const char *yaml = "!!set {a: ~, b: ~}";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SET);
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, SetInvalidValue) {
	const char *yaml = "!!set {a: 1}";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlStandardTags, OmapValid) {
	const char *yaml = "!!omap [ {a: 1}, {b: 2} ]";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_OMAP);
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, OmapInvalidEntry) {
	const char *yaml = "!!omap [ {a: 1, b: 2} ]";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlStandardTags, OmapDuplicateKey) {
	const char *yaml = "!!omap [ {a: 1}, {a: 2} ]";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_DUPKEY);
}

TEST(YamlStandardTags, PairsValid) {
	const char *yaml = "!!pairs [ {a: 1}, {a: 2} ]";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_PAIRS);
	gtext_yaml_free(doc);
}

TEST(YamlStandardTags, PairsInvalidEntry) {
	const char *yaml = "!!pairs [ {a: 1, b: 2} ]";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

// ---------------------------------------------------------------------------
// Where a tag attaches
//
// A tag applies to the node that follows it.  The scanner can only attach a
// pending tag to the next scalar, because in block context nothing yet says
// whether the node starting there is that scalar or a collection whose first
// key or item it is.  Tags on block collections were therefore dropped: the
// tag stayed on the key or the first item and the collection came out
// untagged, for standard and application tags alike, at the document root and
// nested.
//
// The two shapes that decide it differ in nothing but a line break:
//
//     !custom a: 1     tags the key
//     !custom
//     a: 1             tags the mapping
//
// so GTEXT_YAML_Event carries the line the tag was written on, and the parser
// moves an own-line tag onto the collection it turns out to begin.
//
// Every expectation below was taken from PyYAML's composer on the same input.
// ---------------------------------------------------------------------------

namespace {

const GTEXT_YAML_Node * tag_first_key(const GTEXT_YAML_Node *map) {
	const GTEXT_YAML_Node *k = nullptr;
	if (!gtext_yaml_mapping_get_at(map, 0, &k, nullptr)) {
		return nullptr;
	}
	return k;
}

const char *tag_or_dash(const GTEXT_YAML_Node *n) {
	if (!n) {
		return "<null>";
	}
	const char *t = gtext_yaml_node_tag(n);
	return t ? t : "-";
}

GTEXT_YAML_Document *parse_or_null(const char *src, GTEXT_YAML_Error *err) {
	return gtext_yaml_parse(src, strlen(src), NULL, err);
}

} // namespace

TEST(YamlTagPlacement, OwnLineTagGoesToTheBlockMapping) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_or_null("!custom\na: 1\n", &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	EXPECT_STREQ(tag_or_dash(root), "!custom");
	// and not left behind on the key
	EXPECT_STREQ(tag_or_dash(tag_first_key(root)), "-");

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlTagPlacement, SameLineTagStaysOnTheKey) {
	// The counterpart, and the reason the line matters: identical events,
	// different meaning.
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_or_null("!custom a: 1\n", &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	EXPECT_STREQ(tag_or_dash(root), "-") << "the mapping took a key's tag";
	EXPECT_STREQ(tag_or_dash(tag_first_key(root)), "!custom");

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlTagPlacement, OwnLineTagGoesToTheBlockSequence) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = parse_or_null("!custom\n- 1\n", &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	EXPECT_STREQ(tag_or_dash(root), "!custom");
	ASSERT_EQ(gtext_yaml_sequence_length(root), 1u);
	EXPECT_STREQ(tag_or_dash(gtext_yaml_sequence_get(root, 0)), "-")
	    << "the tag was left on the first item";

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

TEST(YamlTagPlacement, NestedBlockCollectionsTakeTheirTag) {
	{
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_or_null("x: !custom\n  - 1\n", &err);
		ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
		const GTEXT_YAML_Node *v =
		    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "x");
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(v), GTEXT_YAML_SEQUENCE);
		EXPECT_STREQ(tag_or_dash(v), "!custom");
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
	{
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_or_null("x: !custom\n  a: 1\n", &err);
		ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
		const GTEXT_YAML_Node *v =
		    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "x");
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(v), GTEXT_YAML_MAPPING);
		EXPECT_STREQ(tag_or_dash(v), "!custom");
		EXPECT_STREQ(tag_or_dash(tag_first_key(v)), "-");
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
}

TEST(YamlTagPlacement, ScalarTagsAreUnaffected) {
	// Both spellings already worked and must keep working: a tag on the same
	// line as its scalar, and a tag on its own line before a plain scalar.
	{
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_or_null("x: !custom 5\n", &err);
		ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
		const GTEXT_YAML_Node *v =
		    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "x");
		EXPECT_STREQ(tag_or_dash(v), "!custom");
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
	{
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc = parse_or_null("!custom\nhello\n", &err);
		ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		EXPECT_STREQ(tag_or_dash(root), "!custom");
		gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
}

TEST(YamlTagPlacement, BlockAndFlowAgreeOnTheStandardCollectionTags) {
	// The tags that gtext_yaml_to_json() refuses.  Before the fix the block
	// spellings resolved to a plain sequence or mapping, so the refusal did
	// not happen for them.
	struct Case {
		const char *flow;
		const char *block;
		GTEXT_YAML_Node_Type type;
		const char *tag;
	};
	const Case cases[] = {
	    {"!!omap [{a: 1}]", "!!omap\n- a: 1\n", GTEXT_YAML_OMAP, "!!omap"},
	    {"!!pairs [{a: 1}]", "!!pairs\n- a: 1\n", GTEXT_YAML_PAIRS, "!!pairs"},
	};

	for (const Case &c : cases) {
		SCOPED_TRACE(c.tag);
		for (const char *src : {c.flow, c.block}) {
			SCOPED_TRACE(src);
			GTEXT_YAML_Error err = {};
			GTEXT_YAML_Document *doc = parse_or_null(src, &err);
			ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
			const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
			ASSERT_NE(root, nullptr);
			EXPECT_EQ(gtext_yaml_node_type(root), c.type);
			EXPECT_STREQ(tag_or_dash(root), c.tag);
			gtext_yaml_free(doc);
			gtext_yaml_error_free(&err);
		}
	}
}
