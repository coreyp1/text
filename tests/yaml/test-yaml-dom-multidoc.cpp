/**
 * @file test-yaml-dom-multidoc.cpp
 * @brief Tests for multi-document YAML parsing with DOM API
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

TEST(YamlDomMultiDoc, TwoDocuments) {
	const char *yaml = "---\nfirst: 1\n---\nsecond: 2\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_index(doc), 0u);
	ASSERT_NE(gtext_yaml_document_root(doc), nullptr);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomMultiDoc, ThreeDocuments) {
	const char *yaml = "---\nfirst: 1\n---\nsecond: 2\n---\nthird: 3\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_index(doc), 0u);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomMultiDoc, WithEndMarkers) {
	const char *yaml = "---\nfirst: 1\n...\n---\nsecond: 2\n...\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_index(doc), 0u);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomMultiDoc, SingleDocument) {
	const char *yaml = "---\nsingle: doc\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_index(doc), 0u);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomMultiDoc, ImplicitDocument) {
	const char *yaml = "key: value\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_index(doc), 0u);
	
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
