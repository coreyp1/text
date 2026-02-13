#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_resolver.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

TEST(YamlAliasExponential, DFSLimit) {
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_alias_expansion = 8;
  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);

  const int N = 6;
  char names[N][2];
  for (int i = 0; i < N; ++i) names[i][0] = 'a' + i, names[i][1] = '\0';

  for (int i = N-1; i >= 0; --i) {
    if (i == N-1) {
      int ok = gtext_yaml_resolver_register_anchor_with_refs(r, names[i], 4, NULL, 0);
      ASSERT_TRUE(ok);
    } else {
      const char *refs[] = { names[i+1] };
      int ok = gtext_yaml_resolver_register_anchor_with_refs(r, names[i], 1, refs, 1);
      ASSERT_TRUE(ok);
    }
  }

  size_t out = 0;
  GTEXT_YAML_Status st = gtext_yaml_resolver_compute_expansion(r, "a", opts.max_alias_expansion, &out);
  EXPECT_EQ(st, GTEXT_YAML_E_LIMIT);

  gtext_yaml_resolver_free(r);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
