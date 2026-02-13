// gtest wrapper for existing YAML scanner test
#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
}

static int cb_count = 0;

static GTEXT_YAML_Status dummy_cb(GTEXT_YAML_Stream *s, const void * event_payload, void * user) {
    (void)s; (void)user;
    const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)event_payload;
    (void)e; // no noisy prints
    cb_count++;
    return GTEXT_YAML_OK;
}

TEST(YamlScanner, BasicFeedFinish) {
    const char *input = "foo - bar";

    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, dummy_cb, NULL);
    ASSERT_NE(s, nullptr);

    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);

    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);

    gtext_yaml_stream_free(s);

    EXPECT_GE(cb_count, 3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
