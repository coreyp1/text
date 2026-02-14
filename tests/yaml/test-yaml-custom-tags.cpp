/**
 * @file test-yaml-custom-tags.cpp
 * @brief Tests for custom tag registration.
 */

#include <gtest/gtest.h>
#include <string>
#include <string.h>
#include <ctype.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static bool parse_called = false;

static bool str_eq_ci(const char *a, const char *b) {
	if (!a || !b) return false;
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static GTEXT_YAML_Status custom_bool_constructor(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *node,
	const char *tag,
	void *user,
	GTEXT_YAML_Error *out_err
) {
	(void)doc;
	(void)tag;
	bool *flag = (bool *)user;
	if (flag) *flag = true;
	if (!node || gtext_yaml_node_type(node) != GTEXT_YAML_STRING) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Custom tag requires string scalar";
		}
		return GTEXT_YAML_E_INVALID;
	}

	const char *value = gtext_yaml_node_as_string(node);
	if (!value) {
		if (out_err) {
			out_err->code = GTEXT_YAML_E_INVALID;
			out_err->message = "Custom tag missing scalar value";
		}
		return GTEXT_YAML_E_INVALID;
	}

	if (str_eq_ci(value, "yes")) {
		return gtext_yaml_node_set_bool(node, true)
			? GTEXT_YAML_OK
			: GTEXT_YAML_E_INVALID;
	}
	if (str_eq_ci(value, "no")) {
		return gtext_yaml_node_set_bool(node, false)
			? GTEXT_YAML_OK
			: GTEXT_YAML_E_INVALID;
	}

	if (out_err) {
		out_err->code = GTEXT_YAML_E_INVALID;
		out_err->message = "Custom tag invalid value";
	}
	return GTEXT_YAML_E_INVALID;
}

static GTEXT_YAML_Status custom_tag_representer(
	const GTEXT_YAML_Node *node,
	const char *tag,
	void *user,
	const char **out_tag,
	GTEXT_YAML_Error *out_err
) {
	(void)node;
	(void)tag;
	(void)user;
	(void)out_err;
	if (!out_tag) return GTEXT_YAML_E_INVALID;
	*out_tag = "!custom";
	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Custom_Tag custom_tags[] = {
	{
		.tag = "tag:example.com,2026:bool",
		.construct = custom_bool_constructor,
		.represent = custom_tag_representer,
		.to_json = NULL,
		.user = &parse_called
	}
};

TEST(YamlCustomTags, ConstructorRunsWhenEnabled) {
	parse_called = false;
	const char *yaml =
		"%TAG !e! tag:example.com,2026:\n"
		"---\n"
		"!e!bool yes\n";

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.enable_custom_tags = true;
	opts.custom_tags = custom_tags;
	opts.custom_tag_count = 1;

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	EXPECT_TRUE(parse_called);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_BOOL);
	bool value = false;
	EXPECT_TRUE(gtext_yaml_node_as_bool(root, &value));
	EXPECT_TRUE(value);

	gtext_yaml_free(doc);
}

TEST(YamlCustomTags, ConstructorSkippedWhenDisabled) {
	parse_called = false;
	const char *yaml =
		"%TAG !e! tag:example.com,2026:\n"
		"---\n"
		"!e!bool yes\n";

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.enable_custom_tags = false;
	opts.custom_tags = custom_tags;
	opts.custom_tag_count = 1;

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
	EXPECT_FALSE(parse_called);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);

	gtext_yaml_free(doc);
}

TEST(YamlCustomTags, RepresenterOverridesTag) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(
		doc,
		"yes",
		"tag:example.com,2026:bool",
		NULL
	);
	ASSERT_NE(scalar, nullptr);
	ASSERT_TRUE(gtext_yaml_document_set_root(doc, scalar));

	GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
	opts.enable_custom_tags = true;
	opts.custom_tags = custom_tags;
	opts.custom_tag_count = 1;

	GTEXT_YAML_Sink sink;
	GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	status = gtext_yaml_write_document(doc, &sink, &opts);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	std::string output = gtext_yaml_sink_buffer_data(&sink);
	gtext_yaml_sink_buffer_free(&sink);
	EXPECT_EQ(output, "!custom yes");

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
