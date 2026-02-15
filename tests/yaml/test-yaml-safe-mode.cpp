/**
 * @file test-yaml-safe-mode.cpp
 * @brief Tests for YAML safe-mode parsing.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlSafeMode, AcceptsBasicDocument) {
	const char *yaml = "a: 1\nb: true\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	gtext_yaml_free(doc);
}

TEST(YamlSafeMode, RejectsAliases) {
	const char *yaml = "a: &x 1\nb: *x\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlSafeMode, RejectsMergeKeys) {
	const char *yaml = "a: {<<: {b: 1}, c: 2}\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlSafeMode, RejectsCustomTags) {
	const char *yaml = "!custom 1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlSafeMode, RejectsComplexKeys) {
	const char *yaml = "? [a, b]\n: 1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlSafeMode, RejectsNonStringKeys) {
	const char *yaml = "1: one\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_safe(yaml, strlen(yaml), &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
