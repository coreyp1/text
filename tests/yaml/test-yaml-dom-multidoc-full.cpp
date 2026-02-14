/**
 * @file test-yaml-dom-multidoc-full.cpp
 * @brief Tests for full multi-document YAML parsing with gtext_yaml_parse_all()
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

/**
 * @brief Test parsing 2 documents with explicit markers.
 */
TEST(YamlDomMultiDocFull, TwoDocumentsExplicit) {
	const char *yaml = "---\nfirst: 1\n---\nsecond: 2\n";
	
	size_t count = 0;
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, &err);
	
	ASSERT_NE(docs, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	ASSERT_EQ(count, 2u);
	
	/* Check first document */
	ASSERT_NE(docs[0], nullptr);
	EXPECT_EQ(gtext_yaml_document_index(docs[0]), 0u);
	const GTEXT_YAML_Node *root0 = gtext_yaml_document_root(docs[0]);
	ASSERT_NE(root0, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root0), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(root0), 1u);
	const GTEXT_YAML_Node *key0 = nullptr;
	const GTEXT_YAML_Node *val0 = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root0, 0, &key0, &val0));
	ASSERT_NE(key0, nullptr);
	ASSERT_NE(val0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key0), "first");
	EXPECT_STREQ(gtext_yaml_node_as_string(val0), "1");
	
	/* Check second document */
	ASSERT_NE(docs[1], nullptr);
	EXPECT_EQ(gtext_yaml_document_index(docs[1]), 1u);
	const GTEXT_YAML_Node *root1 = gtext_yaml_document_root(docs[1]);
	ASSERT_NE(root1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root1), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(root1), 1u);
	const GTEXT_YAML_Node *key1 = nullptr;
	const GTEXT_YAML_Node *val1 = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root1, 0, &key1, &val1));
	ASSERT_NE(key1, nullptr);
	ASSERT_NE(val1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key1), "second");
	EXPECT_STREQ(gtext_yaml_node_as_string(val1), "2");
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test parsing 3 documents.
 */
TEST(YamlDomMultiDocFull, ThreeDocuments) {
	const char *yaml = "---\nfirst: 1\n---\nsecond: 2\n---\nthird: 3\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 3u);
	
	/* Verify document indices */
	EXPECT_EQ(gtext_yaml_document_index(docs[0]), 0u);
	EXPECT_EQ(gtext_yaml_document_index(docs[1]), 1u);
	EXPECT_EQ(gtext_yaml_document_index(docs[2]), 2u);
	
	/* Verify content of each document */
	const char *expected_keys[] = {"first", "second", "third"};
	const char *expected_values[] = {"1", "2", "3"};
	
	for (size_t i = 0; i < 3; i++) {
		ASSERT_NE(docs[i], nullptr);
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[i]);
		ASSERT_NE(root, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
		EXPECT_EQ(gtext_yaml_mapping_size(root), 1u);
		const GTEXT_YAML_Node *key = nullptr;
		const GTEXT_YAML_Node *val = nullptr;
		ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &val));
		ASSERT_NE(key, nullptr);
		ASSERT_NE(val, nullptr);
		EXPECT_STREQ(gtext_yaml_node_as_string(key), expected_keys[i]);
		EXPECT_STREQ(gtext_yaml_node_as_string(val), expected_values[i]);
	}
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test documents with end markers.
 */
TEST(YamlDomMultiDocFull, WithEndMarkers) {
	const char *yaml = "---\nfirst: 1\n...\n---\nsecond: 2\n...\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 2u);
	
	/* Verify both documents */
	EXPECT_EQ(gtext_yaml_document_index(docs[0]), 0u);
	EXPECT_EQ(gtext_yaml_document_index(docs[1]), 1u);
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test single document still works.
 */
TEST(YamlDomMultiDocFull, SingleDocument) {
	const char *yaml = "---\nkey: value\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 1u);
	
	ASSERT_NE(docs[0], nullptr);
	EXPECT_EQ(gtext_yaml_document_index(docs[0]), 0u);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[0]);
	ASSERT_NE(root, nullptr);
	
	/* Clean up */
	gtext_yaml_free(docs[0]);
	free(docs);
}

/**
 * @brief Test implicit document (no markers).
 */
TEST(YamlDomMultiDocFull, ImplicitDocument) {
	const char *yaml = "key: value\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 1u);
	
	ASSERT_NE(docs[0], nullptr);
	EXPECT_EQ(gtext_yaml_document_index(docs[0]), 0u);
	
	/* Clean up */
	gtext_yaml_free(docs[0]);
	free(docs);
}

/**
 * @brief Test documents with different types.
 */
TEST(YamlDomMultiDocFull, DifferentTypes) {
	const char *yaml = 
		"---\n"
		"scalar\n"
		"---\n"
		"- item1\n"
		"- item2\n"
		"---\n"
		"key: value\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 3u);
	
	/* First document: scalar */
	const GTEXT_YAML_Node *root0 = gtext_yaml_document_root(docs[0]);
	ASSERT_NE(root0, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root0), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(root0), "scalar");
	
	/* Second document: sequence */
	const GTEXT_YAML_Node *root1 = gtext_yaml_document_root(docs[1]);
	ASSERT_NE(root1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root1), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(root1), 2u);
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root1, 0);
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root1, 1);
	ASSERT_NE(item0, nullptr);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "item1");
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "item2");
	
	/* Third document: mapping */
	const GTEXT_YAML_Node *root2 = gtext_yaml_document_root(docs[2]);
	ASSERT_NE(root2, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root2), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(root2), 1u);
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test empty documents.
 */
TEST(YamlDomMultiDocFull, EmptyDocuments) {
	const char *yaml = "---\n---\n---\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 3u);
	
	/* All documents should be empty (null root) */
	for (size_t i = 0; i < count; i++) {
		ASSERT_NE(docs[i], nullptr);
		EXPECT_EQ(gtext_yaml_document_index(docs[i]), i);
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[i]);
		EXPECT_EQ(root, nullptr);
	}
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test anchors and aliases within single document only.
 */
TEST(YamlDomMultiDocFull, AnchorsWithinDocument) {
	const char *yaml = 
		"---\n"
		"anchor: &ref value\n"
		"alias: *ref\n"
		"---\n"
		"different: data\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 2u);
	
	/* First document should have anchor/alias resolved */
	const GTEXT_YAML_Node *root0 = gtext_yaml_document_root(docs[0]);
	ASSERT_NE(root0, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root0), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(root0), 2u);
	
	const GTEXT_YAML_Node *anchor_val = gtext_yaml_mapping_get(root0, "anchor");
	const GTEXT_YAML_Node *alias_val = gtext_yaml_mapping_get(root0, "alias");
	ASSERT_NE(anchor_val, nullptr);
	ASSERT_NE(alias_val, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(anchor_val), "value");
	const GTEXT_YAML_Node *alias_target = gtext_yaml_alias_target(alias_val);
	ASSERT_NE(alias_target, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(alias_target), "value");
	
	/* Second document is independent */
	const GTEXT_YAML_Node *root1 = gtext_yaml_document_root(docs[1]);
	ASSERT_NE(root1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root1), GTEXT_YAML_MAPPING);
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test complex nested structures in multiple documents.
 */
TEST(YamlDomMultiDocFull, ComplexNestedStructures) {
	const char *yaml = 
		"---\n"
		"users: [{name: Alice, age: 30}, {name: Bob, age: 25}]\n"
		"---\n"
		"config: {server: {host: localhost, port: 8080}, debug: true}\n";
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 2u);
	
	/* First document: users list */
	const GTEXT_YAML_Node *root0 = gtext_yaml_document_root(docs[0]);
	ASSERT_NE(root0, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root0), GTEXT_YAML_MAPPING);
	const GTEXT_YAML_Node *users = gtext_yaml_mapping_get(root0, "users");
	ASSERT_NE(users, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(users), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(users), 2u);
	
	/* Second document: config tree */
	const GTEXT_YAML_Node *root1 = gtext_yaml_document_root(docs[1]);
	ASSERT_NE(root1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root1), GTEXT_YAML_MAPPING);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root1, "config");
	ASSERT_NE(config, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(config), GTEXT_YAML_MAPPING);
	const GTEXT_YAML_Node *server = gtext_yaml_mapping_get(config, "server");
	ASSERT_NE(server, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(server), GTEXT_YAML_MAPPING);
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

/**
 * @brief Test error handling: NULL input.
 */
TEST(YamlDomMultiDocFull, ErrorNullInput) {
	size_t count = 0;
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(nullptr, 0, &count, nullptr, &err);
	
	EXPECT_EQ(docs, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

/**
 * @brief Test error handling: NULL document_count.
 */
TEST(YamlDomMultiDocFull, ErrorNullDocumentCount) {
	const char *yaml = "key: value\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml, strlen(yaml), nullptr, nullptr, &err);
	
	EXPECT_EQ(docs, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

/**
 * @brief Test many documents (stress test).
 */
TEST(YamlDomMultiDocFull, ManyDocuments) {
	/* Generate YAML with 20 documents */
	std::string yaml;
	for (int i = 0; i < 20; i++) {
		yaml += "---\ndoc" + std::to_string(i) + ": " + std::to_string(i) + "\n";
	}
	
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(yaml.c_str(), yaml.length(), &count, nullptr, nullptr);
	
	ASSERT_NE(docs, nullptr);
	ASSERT_EQ(count, 20u);
	
	/* Verify all documents */
	for (size_t i = 0; i < count; i++) {
		ASSERT_NE(docs[i], nullptr);
		EXPECT_EQ(gtext_yaml_document_index(docs[i]), i);
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[i]);
		ASSERT_NE(root, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	}
	
	/* Clean up */
	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
