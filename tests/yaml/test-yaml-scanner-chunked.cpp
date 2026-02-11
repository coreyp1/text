#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <string.h>
}

static int cb_count = 0;

static GTEXT_YAML_Status dummy_cb(GTEXT_YAML_Stream *s, const void * event_payload, void * user) {
    (void)s; (void)user;
    const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)event_payload;
    if (e->type == GTEXT_YAML_EVENT_SCALAR) {
        if (cb_count == 0) {
            EXPECT_EQ(e->data.scalar.len, (size_t)3);
            EXPECT_EQ(strncmp(e->data.scalar.ptr, "foo", 3), 0);
        } else if (cb_count == 2) {
            EXPECT_EQ(e->data.scalar.len, (size_t)3);
            EXPECT_EQ(strncmp(e->data.scalar.ptr, "bar", 3), 0);
        }
    } else if (e->type == GTEXT_YAML_EVENT_INDICATOR) {
        if (cb_count == 1) {
            EXPECT_EQ(e->data.indicator, '-');
        }
    }
    cb_count++;
    return GTEXT_YAML_OK;
}

TEST(YamlScannerChunked, OneByteFeed) {
    const char *input = "foo - bar";
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, dummy_cb, NULL);
    ASSERT_NE(s, nullptr);
    for (size_t i = 0; i < strlen(input); ++i) {
        GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, &input[i], 1);
        EXPECT_EQ(st, GTEXT_YAML_OK);
    }
    GTEXT_YAML_Status st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
    EXPECT_GE(cb_count, 3);
}
