/**
 * @file test-yaml-directives.cpp
 * @brief Tests for %YAML and %TAG directive events.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml.h>
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

/* A directive belongs to the document that follows it. It has to open a
   document so the parser has somewhere to record it, and that document was
   then being closed by the '---' and handed back as a null in front of the
   real one - so "%YAML 1.2" over "--- text" parsed as two documents, the
   first empty. Every case in yaml-test-suite that opens with a directive
   failed on this, 17 of them. */
TEST(YamlDirectives, ADirectiveDoesNotMakeADocumentOfItsOwn) {
	struct Case {
		const char *input;
		size_t want_docs;
		const char *want_first;  /* nullptr for a null root */
	};
	static const Case kCases[] = {
		{"%YAML 1.2\n--- text\n", 1, "text"},
		{"%TAG !e! tag:example.com,2000:app/\n--- !e!foo bar\n", 1, "bar"},
		{"%YAML 1.2\n---\n", 1, nullptr},
		{"%YAML 1.2\n--- one\n--- two\n", 2, "one"},
		/* No directive: two '---' really are two documents. */
		{"---\n---\n", 2, nullptr},
		{"--- one\n--- two\n", 2, "one"},
	};

	for (const Case &c : kCases) {
		size_t count = 0;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document **docs =
			gtext_yaml_parse_all(c.input, strlen(c.input), &count, nullptr, &err);
		ASSERT_NE(docs, nullptr)
			<< c.input << ": " << (err.message ? err.message : "?");
		EXPECT_EQ(count, c.want_docs) << c.input;
		if (count > 0) {
			const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[0]);
			if (c.want_first) {
				ASSERT_NE(root, nullptr) << c.input;
				EXPECT_STREQ(gtext_yaml_node_as_string(root), c.want_first)
					<< c.input;
			}
			else {
				EXPECT_TRUE(root == nullptr
					|| gtext_yaml_node_type(root) == GTEXT_YAML_NULL) << c.input;
			}
		}
		for (size_t i = 0; i < count; ++i) gtext_yaml_free(docs[i]);
		free(docs);
	}
}

/* A directive has to be followed by a document (6.8). On its own it was
   accepted, as a null document. */
TEST(YamlDirectives, ADirectiveWithNoDocumentIsRefused) {
	static const char *const kInputs[] = {
		"%YAML 1.2\n",
		"%TAG !e! tag:example.com,2000:app/\n",
		"%FOO bar\n",
	};
	for (const char *input : kInputs) {
		size_t count = 0;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document **docs =
			gtext_yaml_parse_all(input, strlen(input), &count, nullptr, &err);
		EXPECT_EQ(docs, nullptr) << input;
		if (docs) {
			for (size_t i = 0; i < count; ++i) gtext_yaml_free(docs[i]);
			free(docs);
		}
	}
}

/* A directive belongs to the prologue of a document: it may only follow the
   start of the stream or a "..." that closed the one before (9.2). One
   arriving after content was being applied to the document already underway.

   What counts as "after content" is not simply "at column 0 after a
   document started": a "%" cannot BEGIN a plain scalar (ns-plain-first
   excludes it) but may appear on a continuation line (ns-plain-char does
   not), so "--- scalar" over "%YAML 1.2" is the single scalar
   "scalar %YAML 1.2" and not a directive at all - suite case XLQ9, and
   js-yaml reads it the same way. The line is a directive only where the
   content above it has already ended, which for these is the dedent to
   column 0 out of a mapping. */
TEST(YamlDirectives, ADirectiveAfterContentIsRefused) {
	static const char *const kRefused[] = {
		"---\nkey: value\n%YAML 1.2\n---\n",
		"a: 1\n%TAG !e! tag:example.com,2000:\n---\n",
		"%YAML 1.2 foo\n---\n",           /* one parameter only (6.8.1) */
		"%YAML 1.2\n%YAML 1.2\n---\n",    /* and one directive per document */
	};
	for (const char *input : kRefused) {
		size_t count = 0;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document **docs =
			gtext_yaml_parse_all(input, strlen(input), &count, nullptr, &err);
		EXPECT_EQ(docs, nullptr) << input;
		if (docs) {
			for (size_t i = 0; i < count; ++i) gtext_yaml_free(docs[i]);
			free(docs);
		}
	}
}

/* A "%" on a plain scalar's continuation line is content, not a directive. */
TEST(YamlDirectives, APercentOnAContinuationLineIsContent) {
	static const char *const kInputs[] = {
		"---\nscalar\n%YAML 1.2\n",
		"{ matches\n% : 20 }\n",
	};
	for (size_t i = 0; i < sizeof(kInputs) / sizeof(kInputs[0]); ++i) {
		size_t count = 0;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document **docs =
			gtext_yaml_parse_all(kInputs[i], strlen(kInputs[i]), &count, nullptr, &err);
		ASSERT_NE(docs, nullptr) << kInputs[i] << ": " << (err.message ? err.message : "?");
		ASSERT_EQ(count, 1u) << kInputs[i];
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(docs[0]);
		ASSERT_NE(root, nullptr);
		if (i == 0) {
			EXPECT_STREQ(gtext_yaml_node_as_string(root), "scalar %YAML 1.2");
		}
		else {
			const GTEXT_YAML_Node *k = nullptr;
			const GTEXT_YAML_Node *v = nullptr;
			ASSERT_EQ(gtext_yaml_mapping_size(root), 1u);
			gtext_yaml_mapping_get_at(root, 0, &k, &v);
			EXPECT_STREQ(gtext_yaml_node_as_string(k), "matches %");
			EXPECT_STREQ(gtext_yaml_node_as_string(v), "20");
		}
		for (size_t j = 0; j < count; ++j) gtext_yaml_free(docs[j]);
		free(docs);
	}
}

/* With the "..." in place the same directive is fine, and each document gets
   its own. */
TEST(YamlDirectives, ADirectiveAfterADocumentEndIsFine) {
	const char *input = "--- a\n...\n%YAML 1.2\n--- b\n";
	size_t count = 0;
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document **docs =
		gtext_yaml_parse_all(input, strlen(input), &count, nullptr, &err);
	ASSERT_NE(docs, nullptr) << (err.message ? err.message : "?");
	EXPECT_EQ(count, 2u);
	if (count == 2) {
		EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_document_root(docs[0])), "a");
		EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_document_root(docs[1])), "b");
	}
	for (size_t i = 0; i < count; ++i) gtext_yaml_free(docs[i]);
	free(docs);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
