#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

static GTEXT_YAML_Status noop_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user; return GTEXT_YAML_OK;
}

TEST(YamlAlias, MaxAliasBudget) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_alias_expansion = 2;
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    const char *input = "*a *b *c";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    EXPECT_EQ(st, GTEXT_YAML_E_LIMIT);
    gtext_yaml_stream_free(s);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
