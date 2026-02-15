/**
 * @file test-yaml-json-fastpath.cpp
 * @brief Tests for JSON-as-YAML fast path.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlJsonFastPath, BasicObject) {
	const char *json = "{\"a\":1,\"b\":true,\"c\":null,\"d\":\"x\"}";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(json, strlen(json), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 1);

	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(root, "b");
	ASSERT_NE(b, nullptr);
	bool b_value = false;
	EXPECT_TRUE(gtext_yaml_node_as_bool(b, &b_value));
	EXPECT_TRUE(b_value);

	const GTEXT_YAML_Node *c = gtext_yaml_mapping_get(root, "c");
	ASSERT_NE(c, nullptr);
	EXPECT_TRUE(gtext_yaml_node_is_null(c));

	const GTEXT_YAML_Node *d = gtext_yaml_mapping_get(root, "d");
	ASSERT_NE(d, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(d), "x");

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, FallbackWithComment) {
	const char *yaml = "{ \"a\": 1 } # comment\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 1);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, ExplicitJsonParse) {
	const char *json = "[1,2]";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_json(json, strlen(json), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_sequence_length(root), 2u);

	const GTEXT_YAML_Node *first = gtext_yaml_sequence_get(root, 0);
	const GTEXT_YAML_Node *second = gtext_yaml_sequence_get(root, 1);
	int64_t value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(first, &value));
	EXPECT_EQ(value, 1);
	EXPECT_TRUE(gtext_yaml_node_as_int(second, &value));
	EXPECT_EQ(value, 2);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, DuplicateKeysLastWins) {
	const char *json = "{\"a\":1,\"a\":2}";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(json, strlen(json), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 2);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, InvalidJsonFails) {
	const char *json = "{\"a\":1,}";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_json(json, strlen(json), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_NE(err.code, GTEXT_YAML_OK);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
