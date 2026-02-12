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
	
	if (!doc) {
		std::cout << "Parse failed:\n";
		std::cout << "  Code: " << error.code << "\n";
		std::cout << "  Message: " << (error.message ? error.message : "NULL") << "\n";
		std::cout << "  Offset: " << error.offset << "\n";
		std::cout << "  Line: " << error.line << ", Col: " << error.col << "\n";
	} else {
		std::cout << "Parse succeeded\n";
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		if (root) {
			std::cout << "  Root type: " << gtext_yaml_node_type(root) << "\n";
		} else {
			std::cout << "  Root is NULL\n";
		}
		gtext_yaml_free(doc);
	}
	
	ASSERT_NE(doc, nullptr);
}

TEST(YamlDomDebug, ParseSequence) {
	const char *yaml = "[1, 2, 3]";
	
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	
	if (!doc) {
		std::cout << "Parse failed:\n";
		std::cout << "  Code: " << error.code << "\n";
		std::cout << "  Message: " << (error.message ? error.message : "NULL") << "\n";
	} else {
		std::cout << "Parse succeeded\n";
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		if (root) {
			std::cout << "  Root type: " << gtext_yaml_node_type(root) << "\n";
		} else {
			std::cout << "  Root is NULL\n";
		}
		gtext_yaml_free(doc);
	}
}
