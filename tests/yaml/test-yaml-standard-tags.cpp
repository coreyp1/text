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
