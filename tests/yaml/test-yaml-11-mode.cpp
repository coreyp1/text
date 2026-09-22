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
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_INT);
	/* The type on its own says nothing about whether the digits arrived.
	   190*3600 + 20*60 + 30. */
	int64_t value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(root, &value));
	EXPECT_EQ(value, 685230);
	gtext_yaml_free(doc);
}

TEST(Yaml11Mode, DirectiveEnablesSexagesimalFloat) {
	const char *yaml = "%YAML 1.1\n---\n1:20:30.5\n";
	GTEXT_YAML_Document *doc = parse_yaml(yaml, NULL);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_FLOAT);
	double value = 0.0;
	EXPECT_TRUE(gtext_yaml_node_as_float(root, &value));
	EXPECT_DOUBLE_EQ(value, 1 * 3600 + 20 * 60 + 30.5);
	gtext_yaml_free(doc);
}

/* A sexagesimal whole number past what int64_t holds has no integer to be
   converted to, and converting it anyway is undefined - 6.3.1.4 - which on
   x86-64 means INT64_MIN, and that is what came back.
   "1:99999999999999999999999999999999" resolved to the integer
   -9223372036854775808.

   A decimal too large has always been handled: strtoll() reports ERANGE,
   parse_int_value() gives up, and the scalar stays the string it was written
   as. The sexagesimal rows are built in floating point and never asked. They
   answer the same way now - and the same way as each other, which is the
   half of this that a type check alone would not have caught. */
TEST(Yaml11Mode, ASexagesimalTooLargeForTheTypeIsNotAnInt) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.yaml_1_1 = true;

	const char *too_big[] = {
		"1:99999999999999999999999999999999",
		"-1:99999999999999999999999999999999",
		"99999999999999999999999999999999:0",
	};
	for (const char *text : too_big) {
		GTEXT_YAML_Document *doc = parse_yaml(text, &opts);
		ASSERT_NE(doc, nullptr) << text;
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr) << text;
		EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING)
			<< text << " resolved to a number it cannot hold";
		int64_t value = 0;
		EXPECT_FALSE(gtext_yaml_node_as_int(root, &value)) << text;
		gtext_yaml_free(doc);

		/* And the decimal it is being made to match. */
		GTEXT_YAML_Document *dec = parse_yaml("99999999999999999999999", &opts);
		ASSERT_NE(dec, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(dec)),
			GTEXT_YAML_STRING);
		gtext_yaml_free(dec);
	}
}

/* Where the tag says int, there is no string to fall back to, so it is
   refused - which is what "!!int 99999999999999999999999" already does. */
TEST(Yaml11Mode, AnExplicitIntTagRefusesASexagesimalTooLargeForTheType) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.yaml_1_1 = true;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	const char *text = "!!int 1:99999999999999999999999999999999";
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(text, strlen(text), &opts, &err);
	EXPECT_EQ(doc, nullptr);
	if (doc) gtext_yaml_free(doc);
	else EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
	gtext_yaml_error_free(&err);

	/* One that does fit still resolves, so this is a bound and not a ban. */
	memset(&err, 0, sizeof(err));
	const char *ok = "!!int 1:30";
	doc = gtext_yaml_parse(ok, strlen(ok), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
	int64_t value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(gtext_yaml_document_root(doc), &value));
	EXPECT_EQ(value, 90);
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
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
