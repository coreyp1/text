/**
 * @file test-yaml-dom-debug.cpp
 * @brief Debug test to see what parser returns
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlDomDebug, ParseScalar) {
	const char *yaml = "hello";
	
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomDebug, ParseSequence) {
	const char *yaml = "[1, 2, 3]";
	
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
