#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"
}

TEST(YamlMerge, SingleMapping) {
	const char *yaml =
		"defaults: &def {a: 1, b: 2}\n"
		"config: {<<: *def, b: 3, c: 4}\n";

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root, "config");
	ASSERT_NE(config, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(config), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(config), 3u);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(config, "a");
	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(config, "b");
	const GTEXT_YAML_Node *c = gtext_yaml_mapping_get(config, "c");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	ASSERT_NE(c, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(b), "3");
	EXPECT_STREQ(gtext_yaml_node_as_string(c), "4");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, BlockMappingRoot) {
	const char *yaml =
		"defaults: &def {a: 1, b: 2}\n"
		"<<: *def\n"
		"b: 3\n"
		"c: 4\n";

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 4u);

	const GTEXT_YAML_Node *defaults = gtext_yaml_mapping_get(root, "defaults");
	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(root, "b");
	const GTEXT_YAML_Node *c = gtext_yaml_mapping_get(root, "c");
	ASSERT_NE(defaults, nullptr);
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	ASSERT_NE(c, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(b), "3");
	EXPECT_STREQ(gtext_yaml_node_as_string(c), "4");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, SequenceSources) {
	const char *yaml =
		"base1: &b1 {a: 1, b: 2}\n"
		"base2: &b2 {b: 3, c: 4}\n"
		"config: {<<: [*b1, *b2], d: 5}\n";

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root, "config");
	ASSERT_NE(config, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(config, "a");
	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(config, "b");
	const GTEXT_YAML_Node *c = gtext_yaml_mapping_get(config, "c");
	const GTEXT_YAML_Node *d = gtext_yaml_mapping_get(config, "d");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	ASSERT_NE(c, nullptr);
	ASSERT_NE(d, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(b), "3");
	EXPECT_STREQ(gtext_yaml_node_as_string(c), "4");
	EXPECT_STREQ(gtext_yaml_node_as_string(d), "5");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, TaggedMergeKey) {
	const char *yaml =
		"base: &b {a: 1, b: 2}\n"
		"config: {!!merge <<: *b, b: 3}\n";

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root, "config");
	ASSERT_NE(config, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(config, "a");
	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(config, "b");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(b), "3");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, AliasSource) {
	const char *yaml =
		"base: &b {a: 1}\n"
		"config: {<<: *b, a: 2}\n";

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root, "config");
	ASSERT_NE(config, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(config, "a");
	ASSERT_NE(a, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "2");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, DupkeyPolicyAllowsMergeOverride) {
	const char *yaml =
		"base1: &b1 {a: 1}\n"
		"base2: &b2 {a: 2}\n"
		"config: {<<: [*b1, *b2]}\n";

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_ERROR;

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *config = gtext_yaml_mapping_get(root, "config");
	ASSERT_NE(config, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(config, "a");
	ASSERT_NE(a, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(a), "2");

	gtext_yaml_free(doc);
}

TEST(YamlMerge, DupkeyPolicyStillErrorsOnExplicitDupes) {
	const char *yaml =
		"base: &b {a: 1}\n"
		"config: {<<: *b, a: 2, a: 3}\n";

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_ERROR;

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_DUPKEY);
}

TEST(YamlMerge, InvalidMergeValue) {
	const char *yaml = "a: {<<: [1, 2]}";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

/* A key is a merge key because its *contents* resolve to
   tag:yaml.org,2002:merge - and only a plain scalar is resolved by its
   contents (10.3.2). '"<<"' is the two-character string, which is what both
   PyYAML and js-yaml say, and taking it for a merge key was the usual two
   faults at once: '{"<<": 1}' was refused for a merge value that is not a
   mapping, and '{"<<": {a: 1}}' was *merged* - the key vanished and its
   contents were spliced into the mapping around it, with nothing reported.

   An explicit !!merge tag still says so whatever the style, because then it
   is the tag and not the contents doing the resolving. */
TEST(YamlMerge, AQuotedMergeKeyIsAnOrdinaryString) {
	struct Case { const char *yaml; const char *expected; };
	const Case cases[] = {
		{ "{\"<<\": 1}", "{\"<<\": 1}" },
		{ "{'<<': 1}", "{\"<<\": 1}" },
		{ "{\"<<\": {a: 1}}", "{\"<<\": {\"a\": 1}}" },
		{ "a: &x {p: 1}\nb: {\"<<\": *x}\n",
		  "{\"a\": {\"p\": 1}, \"b\": {\"<<\": {\"p\": 1}}}" },

		/* Plain, and it merges. */
		{ "a: &x {p: 1}\nb: {<<: *x}\n",
		  "{\"a\": {\"p\": 1}, \"b\": {\"p\": 1}}" },
		/* Tagged, and it merges whatever the style. */
		{ "a: &x {p: 1}\nb: {!!merge \"<<\": *x}\n",
		  "{\"a\": {\"p\": 1}, \"b\": {\"p\": 1}}" },
		/* A plain "<<" whose value is not a mapping is still an error. */
		{ "{<<: 1}", nullptr },
	};
	for (const Case &c : cases) {
		const std::string got = Render(c.yaml);
		if (c.expected) {
			EXPECT_EQ(got, std::string(c.expected))
				<< "input: " << ::testing::PrintToString(std::string(c.yaml));
		} else {
			EXPECT_EQ(got, std::string(""))
				<< "should have been refused, input: "
				<< ::testing::PrintToString(std::string(c.yaml));
		}
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
