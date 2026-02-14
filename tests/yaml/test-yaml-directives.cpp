/**
 * @file test-yaml-directives.cpp
 * @brief Tests for %YAML and %TAG directive events.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
}

struct DirectiveCapture {
	int count;
	char names[4][16];
	char values[4][64];
	char values2[4][128];
};

static GTEXT_YAML_Status capture_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
	(void)s;
	DirectiveCapture *cap = (DirectiveCapture *)user;
	const GTEXT_YAML_Event *ev = (const GTEXT_YAML_Event *)evp;

	if (ev->type == GTEXT_YAML_EVENT_DIRECTIVE && cap->count < 4) {
		const char *name = ev->data.directive.name ? ev->data.directive.name : "";
		const char *val = ev->data.directive.value ? ev->data.directive.value : "";
		const char *val2 = ev->data.directive.value2 ? ev->data.directive.value2 : "";

		snprintf(cap->names[cap->count], sizeof(cap->names[cap->count]), "%s", name);
		snprintf(cap->values[cap->count], sizeof(cap->values[cap->count]), "%s", val);
		snprintf(cap->values2[cap->count], sizeof(cap->values2[cap->count]), "%s", val2);
		cap->count++;
	}

	return GTEXT_YAML_OK;
}

TEST(YamlDirectives, EmitsYamlAndTagDirectives) {
	const char *yaml =
		"%YAML 1.2\n"
		"%TAG !e! tag:example.com,2026:\n"
		"---\n"
		"foo: bar\n";

	DirectiveCapture cap;
	memset(&cap, 0, sizeof(cap));
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, &cap);
	ASSERT_NE(stream, nullptr);

	EXPECT_EQ(gtext_yaml_stream_feed(stream, yaml, strlen(yaml)), GTEXT_YAML_OK);
	EXPECT_EQ(gtext_yaml_stream_finish(stream), GTEXT_YAML_OK);
	gtext_yaml_stream_free(stream);

	ASSERT_EQ(cap.count, 2);
	EXPECT_STREQ(cap.names[0], "YAML");
	EXPECT_STREQ(cap.values[0], "1.2");
	EXPECT_STREQ(cap.names[1], "TAG");
	EXPECT_STREQ(cap.values[1], "!e!");
	EXPECT_STREQ(cap.values2[1], "tag:example.com,2026:");
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
