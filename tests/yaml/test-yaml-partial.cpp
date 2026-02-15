/**
 * @file test-yaml-partial.cpp
 * @brief Tests for partial parsing and error recovery.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlPartial, RecoversTopLevelNodes) {
	const char *yaml =
		":\n"
		"next: 2\n";

	GTEXT_YAML_Document *doc = NULL;
	GTEXT_YAML_Error *errors = NULL;
	size_t error_count = 0;
	GTEXT_YAML_Error fatal;
	memset(&fatal, 0, sizeof(fatal));

	GTEXT_YAML_Status status = gtext_yaml_parse_partial(
		yaml,
		strlen(yaml),
		NULL,
		&doc,
		&errors,
		&error_count,
		&fatal
	);
	ASSERT_EQ(status, GTEXT_YAML_OK);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(error_count, 1u);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(root), 2u);

	const GTEXT_YAML_Node *err_node = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(err_node, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(err_node), GTEXT_YAML_STRING);
	const char *err_value = gtext_yaml_node_as_string(err_node);
	ASSERT_NE(err_value, nullptr);
	EXPECT_NE(strstr(err_value, "error:"), nullptr);

	const GTEXT_YAML_Node *map_node = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(map_node, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(map_node), GTEXT_YAML_MAPPING);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(map_node, "next");
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_INT);

	for (size_t i = 0; i < error_count; i++) {
		gtext_yaml_error_free(&errors[i]);
	}
	free(errors);
	gtext_yaml_free(doc);
}

TEST(YamlPartial, CollectsMultipleErrors) {
	const char *yaml =
		":\n"
		":\n"
		"ok: 1\n";

	GTEXT_YAML_Document *doc = NULL;
	GTEXT_YAML_Error *errors = NULL;
	size_t error_count = 0;
	GTEXT_YAML_Error fatal;
	memset(&fatal, 0, sizeof(fatal));

	GTEXT_YAML_Status status = gtext_yaml_parse_partial(
		yaml,
		strlen(yaml),
		NULL,
		&doc,
		&errors,
		&error_count,
		&fatal
	);
	ASSERT_EQ(status, GTEXT_YAML_OK);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(error_count, 2u);

	for (size_t i = 0; i < error_count; i++) {
		gtext_yaml_error_free(&errors[i]);
	}
	free(errors);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
