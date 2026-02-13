#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_resolver.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

TEST(YamlAliasCycle, DetectCycle) {
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);

  const char *refs_a[] = { "b" };
  const char *refs_b[] = { "a" };
  int ok = gtext_yaml_resolver_register_anchor_with_refs(r, "a", 1, refs_a, 1);
  ASSERT_TRUE(ok);
  ok = gtext_yaml_resolver_register_anchor_with_refs(r, "b", 1, refs_b, 1);
  ASSERT_TRUE(ok);

  size_t out = 0;
  GTEXT_YAML_Status st = gtext_yaml_resolver_compute_expansion(r, "a", 0, &out);
  EXPECT_EQ(st, GTEXT_YAML_E_INVALID);

  gtext_yaml_resolver_free(r);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
