#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

static GTEXT_YAML_Status cb_stats(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user; return GTEXT_YAML_OK;
}

TEST(YamlLimits, DefaultsAndTotalBytes) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    EXPECT_EQ(opts.max_depth, 256);
    EXPECT_EQ(opts.max_total_bytes, 64 * 1024 * 1024);
    EXPECT_EQ(opts.max_alias_expansion, 10000);

    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    const char *chunk = "foo\n";
    size_t chunk_len = strlen(chunk);
    for (size_t i = 0; i < 1000; ++i) {
        GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, chunk, chunk_len);
        EXPECT_EQ(st, GTEXT_YAML_OK);
    }
    GTEXT_YAML_Status st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);

    opts.max_total_bytes = 10;
    s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    GTEXT_YAML_Status st2 = gtext_yaml_stream_feed(s, "this is longer than ten", strlen("this is longer than ten"));
    EXPECT_TRUE(st2 == GTEXT_YAML_E_LIMIT || st2 == GTEXT_YAML_E_INVALID || st2 == GTEXT_YAML_E_STATE);
    gtext_yaml_stream_free(s);
}
