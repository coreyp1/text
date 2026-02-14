/**
 * @file test-yaml-dom-accessors.cpp
 * @brief Comprehensive tests for Phase 4.3 DOM collection accessor APIs.
 *
 * Tests sequence accessors, mapping accessors, metadata accessors,
 * edge cases, and iterator functionality.
 */

#include <gtest/gtest.h>
#include <ghoti.io/text/yaml.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Scalar Accessor Tests
 * ============================================================================ */

TEST(YamlDomAccessors, ScalarBoolAccessor) {
	const char *yaml = "true";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	bool value = false;
	EXPECT_TRUE(gtext_yaml_node_as_bool(root, &value));
	EXPECT_TRUE(value);

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, ScalarIntAccessor) {
	const char *yaml = "42";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	int64_t value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(root, &value));
	EXPECT_EQ(value, 42);

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, ScalarFloatAccessor) {
	const char *yaml = "3.25";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	double value = 0.0;
	EXPECT_TRUE(gtext_yaml_node_as_float(root, &value));
	EXPECT_DOUBLE_EQ(value, 3.25);

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, ScalarAccessorWrongType) {
	const char *yaml = "hello";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	bool bool_value = false;
	int64_t int_value = 0;
	double float_value = 0.0;
	EXPECT_FALSE(gtext_yaml_node_as_bool(root, &bool_value));
	EXPECT_FALSE(gtext_yaml_node_as_int(root, &int_value));
	EXPECT_FALSE(gtext_yaml_node_as_float(root, &float_value));

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, ScalarNullAccessor) {
	const char *yaml = "~";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	EXPECT_TRUE(gtext_yaml_node_is_null(root));

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, ScalarTimestampAccessor) {
	const char *yaml = "!!timestamp 2025-02-14T10:30:45Z";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	GTEXT_YAML_Timestamp ts = {};
	EXPECT_TRUE(gtext_yaml_node_as_timestamp(root, &ts));
	EXPECT_EQ(ts.year, 2025);
	EXPECT_EQ(ts.month, 2);
	EXPECT_EQ(ts.day, 14);
	EXPECT_TRUE(ts.has_time);
	EXPECT_EQ(ts.hour, 10);
	EXPECT_EQ(ts.minute, 30);
	EXPECT_EQ(ts.second, 45);
	EXPECT_TRUE(ts.tz_specified);
	EXPECT_TRUE(ts.tz_utc);
	EXPECT_EQ(ts.tz_offset, 0);

	gtext_yaml_free(doc);
}

/* ============================================================================
 * Sequence Accessor Tests
 * ============================================================================ */

TEST(YamlDomAccessors, SequenceLength) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[1, 2, 3]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	EXPECT_EQ(gtext_yaml_sequence_length(root), 3);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, SequenceGetValid) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[first, second, third]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "first");
	
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "second");
	
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 2);
	ASSERT_NE(item2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item2), "third");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, SequenceGetOutOfBounds) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[a, b]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_sequence_get(root, 2), nullptr);
	EXPECT_EQ(gtext_yaml_sequence_get(root, 100), nullptr);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, SequenceGetWrongType) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "scalar";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_sequence_length(root), 0);
	EXPECT_EQ(gtext_yaml_sequence_get(root, 0), nullptr);
	
	gtext_yaml_free(doc);
}

struct SequenceIteratorData {
	int count;
	const char *expected[3];
};

static bool sequence_iterator_callback(const GTEXT_YAML_Node *node, size_t index, void *user) {
	SequenceIteratorData *data = (SequenceIteratorData *)user;
	
	EXPECT_LT(index, 3);
	const char *value = gtext_yaml_node_as_string(node);
	EXPECT_NE(value, nullptr);
	EXPECT_STREQ(value, data->expected[index]);
	
	data->count++;
	return true;  /* Continue iteration */
}

TEST(YamlDomAccessors, SequenceIterateFullTraversal) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[alpha, beta, gamma]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	SequenceIteratorData data = {0, {"alpha", "beta", "gamma"}};
	size_t visited = gtext_yaml_sequence_iterate(root, sequence_iterator_callback, &data);
	
	EXPECT_EQ(visited, 3);
	EXPECT_EQ(data.count, 3);
	
	gtext_yaml_free(doc);
}

static bool sequence_early_stop_callback(const GTEXT_YAML_Node *node, size_t index, void *user) {
	(void)node;  /* Unused */
	int *count = (int *)user;
	(*count)++;
	return index < 1;  /* Stop after second item (index 0 and 1) */
}

TEST(YamlDomAccessors, SequenceIterateEarlyStop) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[1, 2, 3, 4, 5]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	int count = 0;
	size_t visited = gtext_yaml_sequence_iterate(root, sequence_early_stop_callback, &count);
	
	EXPECT_EQ(visited, 2);  /* Stopped at index 2 */
	EXPECT_EQ(count, 2);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, SequenceEmpty) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_sequence_length(root), 0);
	EXPECT_EQ(gtext_yaml_sequence_get(root, 0), nullptr);
	
	int count = 0;
	size_t visited = gtext_yaml_sequence_iterate(root, sequence_early_stop_callback, &count);
	EXPECT_EQ(visited, 0);
	EXPECT_EQ(count, 0);
	
	gtext_yaml_free(doc);
}

/* ============================================================================
 * Mapping Accessor Tests
 * ============================================================================ */

TEST(YamlDomAccessors, MappingSize) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{name: Alice, age: 30, city: NYC}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	EXPECT_EQ(gtext_yaml_mapping_size(root), 3);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingGetByKey) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{first: 1, second: 2, third: 3}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *val1 = gtext_yaml_mapping_get(root, "first");
	ASSERT_NE(val1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(val1), "1");
	
	const GTEXT_YAML_Node *val2 = gtext_yaml_mapping_get(root, "second");
	ASSERT_NE(val2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(val2), "2");
	
	const GTEXT_YAML_Node *val3 = gtext_yaml_mapping_get(root, "third");
	ASSERT_NE(val3, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(val3), "3");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingGetKeyNotFound) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{a: 1, b: 2}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_mapping_get(root, "c"), nullptr);
	EXPECT_EQ(gtext_yaml_mapping_get(root, "nonexistent"), nullptr);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingGetAtValid) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{x: 10, y: 20}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *key0 = nullptr;
	const GTEXT_YAML_Node *val0 = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key0, &val0));
	ASSERT_NE(key0, nullptr);
	ASSERT_NE(val0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key0), "x");
	EXPECT_STREQ(gtext_yaml_node_as_string(val0), "10");
	
	const GTEXT_YAML_Node *key1 = nullptr;
	const GTEXT_YAML_Node *val1 = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 1, &key1, &val1));
	ASSERT_NE(key1, nullptr);
	ASSERT_NE(val1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key1), "y");
	EXPECT_STREQ(gtext_yaml_node_as_string(val1), "20");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingGetAtOutOfBounds) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{k: v}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *val = nullptr;
	EXPECT_FALSE(gtext_yaml_mapping_get_at(root, 1, &key, &val));
	EXPECT_FALSE(gtext_yaml_mapping_get_at(root, 100, &key, &val));
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingGetAtNullOutputs) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{k: v}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* Should succeed even with NULL output pointers */
	EXPECT_TRUE(gtext_yaml_mapping_get_at(root, 0, nullptr, nullptr));
	
	gtext_yaml_free(doc);
}

struct MappingIteratorData {
	int count;
	const char *expected_keys[3];
	const char *expected_values[3];
};

static bool mapping_iterator_callback(
	const GTEXT_YAML_Node *key,
	const GTEXT_YAML_Node *value,
	size_t index,
	void *user
) {
	MappingIteratorData *data = (MappingIteratorData *)user;
	
	EXPECT_LT(index, 3);
	const char *k = gtext_yaml_node_as_string(key);
	const char *v = gtext_yaml_node_as_string(value);
	EXPECT_NE(k, nullptr);
	EXPECT_NE(v, nullptr);
	EXPECT_STREQ(k, data->expected_keys[index]);
	EXPECT_STREQ(v, data->expected_values[index]);
	
	data->count++;
	return true;
}

TEST(YamlDomAccessors, MappingIterateFullTraversal) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{a: 1, b: 2, c: 3}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	MappingIteratorData data = {0, {"a", "b", "c"}, {"1", "2", "3"}};
	size_t visited = gtext_yaml_mapping_iterate(root, mapping_iterator_callback, &data);
	
	EXPECT_EQ(visited, 3);
	EXPECT_EQ(data.count, 3);
	
	gtext_yaml_free(doc);
}

static bool mapping_early_stop_callback(
	const GTEXT_YAML_Node *key,
	const GTEXT_YAML_Node *value,
	size_t index,
	void *user
) {
	(void)key;    /* Unused */
	(void)value;  /* Unused */
	int *count = (int *)user;
	(*count)++;
	return index < 1;  /* Stop after second pair */
}

TEST(YamlDomAccessors, MappingIterateEarlyStop) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{a: 1, b: 2, c: 3, d: 4}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	int count = 0;
	size_t visited = gtext_yaml_mapping_iterate(root, mapping_early_stop_callback, &count);
	
	EXPECT_EQ(visited, 2);
	EXPECT_EQ(count, 2);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingEmpty) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_mapping_size(root), 0);
	EXPECT_EQ(gtext_yaml_mapping_get(root, "key"), nullptr);
	
	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *val = nullptr;
	EXPECT_FALSE(gtext_yaml_mapping_get_at(root, 0, &key, &val));
	
	int count = 0;
	size_t visited = gtext_yaml_mapping_iterate(root, mapping_early_stop_callback, &count);
	EXPECT_EQ(visited, 0);
	EXPECT_EQ(count, 0);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MappingWrongType) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "scalar";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_mapping_size(root), 0);
	EXPECT_EQ(gtext_yaml_mapping_get(root, "key"), nullptr);
	
	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *val = nullptr;
	EXPECT_FALSE(gtext_yaml_mapping_get_at(root, 0, &key, &val));
	
	gtext_yaml_free(doc);
}

/* ============================================================================
 * Nested Collection Access Tests
 * ============================================================================ */

TEST(YamlDomAccessors, NestedSequenceAccess) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[[1, 2], [3, 4], [5, 6]]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(root), 3);
	
	const GTEXT_YAML_Node *seq0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(seq0, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(seq0), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq0, 0)), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq0, 1)), "2");
	
	const GTEXT_YAML_Node *seq1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(seq1, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(seq1), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq1, 0)), "3");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq1, 1)), "4");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, NestedMappingAccess) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{person: {name: Alice, age: 30}, status: active}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *person = gtext_yaml_mapping_get(root, "person");
	ASSERT_NE(person, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(person), GTEXT_YAML_MAPPING);
	
	const GTEXT_YAML_Node *name = gtext_yaml_mapping_get(person, "name");
	ASSERT_NE(name, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(name), "Alice");
	
	const GTEXT_YAML_Node *age = gtext_yaml_mapping_get(person, "age");
	ASSERT_NE(age, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(age), "30");
	
	const GTEXT_YAML_Node *status = gtext_yaml_mapping_get(root, "status");
	ASSERT_NE(status, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(status), "active");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, MixedNesting) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{items: [a, b, c], count: 3}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	const GTEXT_YAML_Node *items = gtext_yaml_mapping_get(root, "items");
	ASSERT_NE(items, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(items), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(items), 3);
	
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(items, 0)), "a");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(items, 1)), "b");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(items, 2)), "c");
	
	const GTEXT_YAML_Node *count = gtext_yaml_mapping_get(root, "count");
	ASSERT_NE(count, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(count), "3");
	
	gtext_yaml_free(doc);
}

/* ============================================================================
 * Set/Omap/Pairs Access Tests
 * ============================================================================ */

TEST(YamlDomAccessors, SetAccessors) {
	const char *yaml = "!!set {a: ~, b: ~}";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SET);
	EXPECT_EQ(gtext_yaml_set_size(root), 2);

	const GTEXT_YAML_Node *first = gtext_yaml_set_get_at(root, 0);
	ASSERT_NE(first, nullptr);
	EXPECT_NE(gtext_yaml_node_as_string(first), nullptr);

	const GTEXT_YAML_Node *second = gtext_yaml_set_get_at(root, 1);
	ASSERT_NE(second, nullptr);
	EXPECT_NE(gtext_yaml_node_as_string(second), nullptr);

	size_t seen = gtext_yaml_set_iterate(
		root,
		[](const GTEXT_YAML_Node *key, size_t, void *) {
			return key != nullptr;
		},
		nullptr
	);
	EXPECT_EQ(seen, 2u);

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, OmapAccessors) {
	const char *yaml = "!!omap [ {a: 1}, {b: 2} ]";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_OMAP);
	EXPECT_EQ(gtext_yaml_omap_size(root), 2);

	const GTEXT_YAML_Node *key = NULL;
	const GTEXT_YAML_Node *value = NULL;
	EXPECT_TRUE(gtext_yaml_omap_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key), "a");
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "1");

	size_t seen = gtext_yaml_omap_iterate(
		root,
		[](const GTEXT_YAML_Node *, const GTEXT_YAML_Node *, size_t, void *) {
			return true;
		},
		nullptr
	);
	EXPECT_EQ(seen, 2u);

	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, PairsAccessors) {
	const char *yaml = "!!pairs [ {a: 1}, {a: 2} ]";
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_PAIRS);
	EXPECT_EQ(gtext_yaml_pairs_size(root), 2);

	const GTEXT_YAML_Node *key = NULL;
	const GTEXT_YAML_Node *value = NULL;
	EXPECT_TRUE(gtext_yaml_pairs_get_at(root, 1, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key), "a");
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "2");

	size_t seen = gtext_yaml_pairs_iterate(
		root,
		[](const GTEXT_YAML_Node *, const GTEXT_YAML_Node *, size_t, void *) {
			return true;
		},
		nullptr
	);
	EXPECT_EQ(seen, 2u);

	gtext_yaml_free(doc);
}

/* ============================================================================
 * Metadata Accessor Tests
 * ============================================================================ */

TEST(YamlDomAccessors, NodeTag) {
	/* Currently streaming parser doesn't emit tags, so we test NULL returns */
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "value";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_node_tag(root), nullptr);
	EXPECT_EQ(gtext_yaml_node_tag(nullptr), nullptr);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, NodeAnchor) {
	/* Currently streaming parser doesn't emit anchors, so we test NULL returns */
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "value";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	EXPECT_EQ(gtext_yaml_node_anchor(root), nullptr);
	EXPECT_EQ(gtext_yaml_node_anchor(nullptr), nullptr);
	
	gtext_yaml_free(doc);
}

/* ============================================================================
 * Edge Cases and Error Handling
 * ============================================================================ */

TEST(YamlDomAccessors, NullNodeHandling) {
	/* All accessor functions should gracefully handle NULL nodes */
	EXPECT_EQ(gtext_yaml_sequence_length(nullptr), 0);
	EXPECT_EQ(gtext_yaml_sequence_get(nullptr, 0), nullptr);
	EXPECT_EQ(gtext_yaml_sequence_iterate(nullptr, sequence_early_stop_callback, nullptr), 0);
	
	EXPECT_EQ(gtext_yaml_mapping_size(nullptr), 0);
	EXPECT_EQ(gtext_yaml_mapping_get(nullptr, "key"), nullptr);
	
	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *val = nullptr;
	EXPECT_FALSE(gtext_yaml_mapping_get_at(nullptr, 0, &key, &val));
	EXPECT_EQ(gtext_yaml_mapping_iterate(nullptr, mapping_early_stop_callback, nullptr), 0);
	
	EXPECT_EQ(gtext_yaml_node_tag(nullptr), nullptr);
	EXPECT_EQ(gtext_yaml_node_anchor(nullptr), nullptr);
}

TEST(YamlDomAccessors, NullCallbackHandling) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "[1, 2, 3]";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* Iterator should handle NULL callback gracefully */
	EXPECT_EQ(gtext_yaml_sequence_iterate(root, nullptr, nullptr), 0);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomAccessors, NullKeyHandling) {
GTEXT_YAML_Error error;
memset(&error, 0, sizeof(error));
	const char *yaml = "{k: v}";
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* Should handle NULL key gracefully */
	EXPECT_EQ(gtext_yaml_mapping_get(root, nullptr), nullptr);
	
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
