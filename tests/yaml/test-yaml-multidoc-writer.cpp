/**
 * @file test-yaml-multidoc-writer.cpp
 * @brief Tests for DOM multi-document writer helper.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlMultiDocWriter, WritesDocumentsWithMarkers) {
	const char *yaml =
		"first: 1\n"
		"---\n"
		"second: 2\n";

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(
		yaml,
		strlen(yaml),
		&count,
		NULL,
		&error
	);
	ASSERT_NE(docs, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");
	ASSERT_EQ(count, 2u);

	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);

	GTEXT_YAML_Write_Options write_opts = gtext_yaml_write_options_default();
	write_opts.pretty = true;
	ASSERT_EQ(gtext_yaml_write_documents(docs, count, &sink, &write_opts), GTEXT_YAML_OK);

	const char *out = gtext_yaml_sink_buffer_data(&sink);
	ASSERT_NE(out, nullptr);

	const char *first_pos = strstr(out, "---\nfirst: 1");
	ASSERT_NE(first_pos, nullptr);
	const char *second_pos = strstr(out, "---\nsecond: 2");
	ASSERT_NE(second_pos, nullptr);
	EXPECT_LT(first_pos, second_pos);

	for (size_t i = 0; i < count; i++) {
		gtext_yaml_free(docs[i]);
	}
	free(docs);
	gtext_yaml_sink_buffer_free(&sink);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
