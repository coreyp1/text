/**
 * @file test-yaml-binary.cpp
 * @brief Tests for !!binary tag support.
 */

#include <gtest/gtest.h>
#include <string>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static std::string write_doc(
	const GTEXT_YAML_Document *doc,
	const GTEXT_YAML_Write_Options *opts
) {
	GTEXT_YAML_Sink sink;
	GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	status = gtext_yaml_write_document(doc, &sink, opts);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	std::string output = gtext_yaml_sink_buffer_data(&sink);
	gtext_yaml_sink_buffer_free(&sink);
	return output;
}

TEST(YamlBinary, DecodeValidBase64) {
	const char *yaml = "!!binary SGVsbG8=";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const unsigned char *data = NULL;
	size_t len = 0;
	EXPECT_TRUE(gtext_yaml_node_as_binary(root, &data, &len));
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(len, 5u);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(data), len), "Hello");
	EXPECT_STREQ(gtext_yaml_node_as_string(root), "SGVsbG8=");

	gtext_yaml_free(doc);
}

TEST(YamlBinary, DecodeIgnoresWhitespace) {
	const char *yaml = "!!binary \"SGVs\n bG8=\"";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const unsigned char *data = NULL;
	size_t len = 0;
	EXPECT_TRUE(gtext_yaml_node_as_binary(root, &data, &len));
	EXPECT_EQ(len, 5u);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(data), len), "Hello");

	gtext_yaml_free(doc);
}

TEST(YamlBinary, DecodeRejectsInvalid) {
	const char *yaml = "!!binary SGVsbG8";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlBinary, WriterEmitsCanonicalBase64) {
	const char *yaml = "!!binary SGVsbG8=";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	std::string output = write_doc(doc, NULL);
	EXPECT_EQ(output, "!!binary SGVsbG8=");

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
