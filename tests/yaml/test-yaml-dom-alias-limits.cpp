#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

TEST(YamlDomAliasLimits, AliasLimitExceeded) {
	const char *yaml = "[&a one, *a, *a]";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.max_alias_expansion = 1;

	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);

	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_LIMIT);
	EXPECT_NE(err.message, nullptr);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
