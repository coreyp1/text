#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_internal.h>
#include <ghoti.io/text/yaml/yaml_resolver.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

TEST(YamlAliasExpansion, BudgetEnforced) {
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_alias_expansion = 5;

  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);

  int ok = gtext_yaml_resolver_register_anchor(r, "a", 3);
  ASSERT_TRUE(ok);

  GTEXT_YAML_Status st = gtext_yaml_resolver_apply_alias(r, "a");
  EXPECT_EQ(st, GTEXT_YAML_OK);
  st = gtext_yaml_resolver_apply_alias(r, "a");
  EXPECT_EQ(st, GTEXT_YAML_E_LIMIT);

  gtext_yaml_resolver_free(r);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
