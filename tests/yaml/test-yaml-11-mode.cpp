/**
 * @file test-yaml-11-mode.cpp
 * @brief Tests for YAML 1.1 compatibility mode.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static GTEXT_YAML_Document *parse_yaml(const char *yaml, GTEXT_YAML_Parse_Options *opts) {
	GTEXT_YAML_Error err = {};
	return gtext_yaml_parse(yaml, strlen(yaml), opts, &err);
}

TEST(Yaml11Mode, DirectiveEnablesBooleans) {
	const char *yaml = "%YAML 1.1\n---\nyes\n";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_BOOL);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, DirectiveEnablesOctal) {
	const char *yaml = "%YAML 1.1\n---\n0755\n";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_INT);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, DefaultTreatsOctalAsString) {
	const char *yaml = "0755";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_STRING);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, DirectiveEnablesSexagesimalInt) {
	const char *yaml = "%YAML 1.1\n---\n190:20:30\n";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_INT);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, DirectiveEnablesSexagesimalFloat) {
	const char *yaml = "%YAML 1.1\n---\n1:20:30.5\n";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_FLOAT);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, OptionForcesCompatibility) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.yaml_1_1 = true;

	GTEXT_YAML_Document *doc = parse_yaml("on", &opts);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_BOOL);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, ExplicitTagOverridesImplicit) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.yaml_1_1 = true;

	GTEXT_YAML_Document *doc = parse_yaml("!!str yes", &opts);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_STRING);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
