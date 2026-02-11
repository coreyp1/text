#include <gtest/gtest.h>

extern "C" {
#include "../src/yaml/yaml_internal.h"
#include <string.h>
}

TEST(YamlUtf8, ValidateVarious) {
    const char *a = "hello";
    EXPECT_EQ(gtext_utf8_validate(a, strlen(a)), 1);

    const char *s = "\xF0\x9F\x98\x80";
    EXPECT_EQ(gtext_utf8_validate(s, 4), 1);

    const char bad1[] = { (char)0x80, 0 };
    EXPECT_EQ(gtext_utf8_validate(bad1, 1), 0);

    const char bad2[] = { (char)0xC0, (char)0x81, 0 };
    EXPECT_EQ(gtext_utf8_validate(bad2, 2), 0);

    const char bad3[] = { (char)0xED, (char)0xA0, (char)0x80, 0 };
    EXPECT_EQ(gtext_utf8_validate(bad3, 3), 0);
}
