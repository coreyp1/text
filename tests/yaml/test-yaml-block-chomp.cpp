#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <stdlib.h>
#include <string.h>
}

static char *last_scalar = NULL;
static size_t last_len = 0;

static GTEXT_YAML_Status capture_cb(GTEXT_YAML_Stream *s, const void * event_payload, void * user) {
    (void)s; (void)user;
    const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)event_payload;
    if (e->type == GTEXT_YAML_EVENT_SCALAR) {
        if (last_scalar) free(last_scalar);
        last_len = e->data.scalar.len;
        last_scalar = (char *)malloc(last_len + 1);
        memcpy(last_scalar, e->data.scalar.ptr, last_len);
        last_scalar[last_len] = '\0';
    }
    return GTEXT_YAML_OK;
}

TEST(YamlBlockChomp, ChompingModes) {
    const char *in1 = "|\n  a\n  b\n";
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    if (last_scalar) { free(last_scalar); last_scalar = NULL; }
    last_len = 0;
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, in1, strlen(in1));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 3);
    EXPECT_STREQ(last_scalar, "a\nb");

    const char *in2 = "|+\n  a\n  b\n\n";
    s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    if (last_scalar) { free(last_scalar); last_scalar = NULL; }
    last_len = 0;
    st = gtext_yaml_stream_feed(s, in2, strlen(in2));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 5);
    EXPECT_EQ(memcmp(last_scalar, "a\nb\n\n", 5), 0);

    const char *in3 = "|-\n  a\n  b\n\n";
    s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    if (last_scalar) { free(last_scalar); last_scalar = NULL; }
    last_len = 0;
    st = gtext_yaml_stream_feed(s, in3, strlen(in3));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 3);
    EXPECT_STREQ(last_scalar, "a\nb");

    if (last_scalar) free(last_scalar);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
