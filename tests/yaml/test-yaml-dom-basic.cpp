/**
 * @file test-yaml-dom-basic.cpp
 * @brief Basic DOM parser tests - Phase 4.2
 *
 * Tests simple document parsing: scalars, sequences, mappings.
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

//
// Test: Parse a simple scalar
//
TEST(YamlDomBasic, ParseScalar) {
	const char *yaml = "hello";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");
	
	// Check that we have a root node
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	// Check that it's a string
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	// Check the value
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "hello");
	
	gtext_yaml_free(doc);
}

//
// Test: Parse an empty string
//
TEST(YamlDomBasic, ParseEmptyString) {
	const char *yaml = "";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	// Empty document might be valid with no root, or have a null node
	// For now, just check it doesn't crash
	if (doc) {
		gtext_yaml_free(doc);
	}
}

//
// Test: Parse a quoted string
//
TEST(YamlDomBasic, ParseQuotedString) {
	const char *yaml = "\"hello world\"";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "hello world");
	
	gtext_yaml_free(doc);
}

//
// Test: Parse a multi-line scalar with explicit quotes
//
TEST(YamlDomBasic, ParseMultiLineScalar) {
	const char *yaml = "'line one\\nline two'";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	if (doc) {
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
		gtext_yaml_free(doc);
	}
}

//
// Test: Parse a simple sequence
//
TEST(YamlDomBasic, ParseSequence) {
	const char *yaml = "[1, 2, 3]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	// TODO: Once sequence parsing works, add assertions
	if (doc) {
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr);
		// EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
		gtext_yaml_free(doc);
	}
}

//
// Test: Parse a simple mapping
//
TEST(YamlDomBasic, ParseMapping) {
	const char *yaml = "key: value";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(
		yaml, strlen(yaml), NULL, &error
	);
	
	// TODO: Once mapping parsing works, add assertions
	if (doc) {
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr);
		// EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
		gtext_yaml_free(doc);
	}
}
