/**
 * @file test-yaml-json-fastpath.cpp
 * @brief Tests for JSON-as-YAML fast path.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlJsonFastPath, BasicObject) {
	const char *json = "{\"a\":1,\"b\":true,\"c\":null,\"d\":\"x\"}";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(json, strlen(json), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 1);

	const GTEXT_YAML_Node *b = gtext_yaml_mapping_get(root, "b");
	ASSERT_NE(b, nullptr);
	bool b_value = false;
	EXPECT_TRUE(gtext_yaml_node_as_bool(b, &b_value));
	EXPECT_TRUE(b_value);

	const GTEXT_YAML_Node *c = gtext_yaml_mapping_get(root, "c");
	ASSERT_NE(c, nullptr);
	EXPECT_TRUE(gtext_yaml_node_is_null(c));

	const GTEXT_YAML_Node *d = gtext_yaml_mapping_get(root, "d");
	ASSERT_NE(d, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(d), "x");

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, FallbackWithComment) {
	const char *yaml = "{ \"a\": 1 } # comment\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 1);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, ExplicitJsonParse) {
	const char *json = "[1,2]";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_json(json, strlen(json), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_sequence_length(root), 2u);

	const GTEXT_YAML_Node *first = gtext_yaml_sequence_get(root, 0);
	const GTEXT_YAML_Node *second = gtext_yaml_sequence_get(root, 1);
	int64_t value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(first, &value));
	EXPECT_EQ(value, 1);
	EXPECT_TRUE(gtext_yaml_node_as_int(second, &value));
	EXPECT_EQ(value, 2);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, DuplicateKeysLastWins) {
	const char *json = "{\"a\":1,\"a\":2}";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(json, strlen(json), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *a = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(a, nullptr);
	int64_t a_value = 0;
	EXPECT_TRUE(gtext_yaml_node_as_int(a, &a_value));
	EXPECT_EQ(a_value, 2);

	gtext_yaml_free(doc);
}

TEST(YamlJsonFastPath, InvalidJsonFails) {
	const char *json = "{\"a\":1,}";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse_json(json, strlen(json), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_NE(err.code, GTEXT_YAML_OK);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

/* ---------------------------------------------------------------------------
 * The fast path is a second parser, and it has to give the same answers.
 *
 * Everything above tests it on its own, which is how it came to disagree with
 * the general parser without anything noticing: a JSON string was turned into
 * a YAML scalar with no style, and the resolver resolves a scalar by its
 * contents only when it was written plain (10.3.2, which is the whole point
 * of quoting). So ["0x10"] came back as [16], [""] as [null], and the key of
 * {"": ""} as the string "null" - while the same documents read correctly the
 * other way.
 *
 * Nothing measured it, either. `make conformance` asks for
 * GTEXT_YAML_DUPKEY_KEEP_ALL, which is the one setting that turns the fast
 * path off, so all 395 suite cases have only ever gone the other way.
 * ------------------------------------------------------------------------ */

#include <string>

extern "C" {
#include <ghoti.io/text/json.h>
}

namespace {

/* The document's values, as JSON, or "(refused)". */
std::string read_with(const std::string &src, bool fast) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.enable_json_fast_path = fast;
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(src.data(), src.size(), &opts, &err);
	if (!doc) {
		std::string why = err.message ? err.message : "refused";
		gtext_yaml_error_free(&err);
		return "(refused: " + why + ")";
	}
	gtext_yaml_error_free(&err);

	GTEXT_JSON_Value *json = nullptr;
	GTEXT_YAML_To_JSON_Options jopts = gtext_yaml_to_json_options_default();
	jopts.allow_resolved_aliases = true;
	jopts.allow_merge_keys = true;
	jopts.coerce_keys_to_strings = true;
	std::string out = "(no json)";
	memset(&err, 0, sizeof(err));
	if (gtext_yaml_to_json_with_options(doc, &json, &jopts, &err) == GTEXT_YAML_OK
			&& json) {
		GTEXT_JSON_Sink sink;
		GTEXT_JSON_Error jerr;
		memset(&jerr, 0, sizeof(jerr));
		if (gtext_json_sink_buffer(&sink) == GTEXT_JSON_OK) {
			if (gtext_json_write_value(&sink, nullptr, json, &jerr) == GTEXT_JSON_OK) {
				out.assign(gtext_json_sink_buffer_data(&sink),
					gtext_json_sink_buffer_size(&sink));
			}
			gtext_json_sink_buffer_free(&sink);
		}
		gtext_json_free(json);
	}
	gtext_yaml_error_free(&err);
	gtext_yaml_free(doc);
	return out;
}

}  // namespace

TEST(YamlJsonFastPath, AgreesWithTheGeneralParser) {
	const char *documents[] = {
		/* The four that disagreed. */
		"[\"\"]",
		"{\"a\":\"\"}",
		"[\"0x10\"]",
		"{\"\":\"\"}",
		/* Every scalar whose text would resolve to something else if the
		   quotes were forgotten. */
		"[\"12\"]", "[\"1.5\"]", "[\"true\"]", "[\"false\"]", "[\"null\"]",
		"[\"~\"]", "[\"0o17\"]", "[\"-0\"]", "[\".inf\"]", "[\".nan\"]",
		"[\"0b101\"]", "[\"1_000\"]", "[\"2001-12-14\"]", "[\"y\"]",
		"[\"on\"]", "[\"No\"]",
		/* And keys, which take the same route. */
		"{\"12\":1}", "{\"true\":1}", "{\"null\":1}",
		/* Ordinary documents, so the test says something when it passes. */
		"[1,2,3]", "{\"a\":1}", "null", "true", "123", "\"x\"",
		"[]", "{}", "[[],{}]", "{\"a\":{\"b\":[1,\"c\",null]}}",
		"[1.0,1e3,-0.5]", "[\"\\u00e9\",\"\\t\",\"\\\\\"]",
	};
	for (const char *src : documents) {
		EXPECT_EQ(read_with(src, true), read_with(src, false))
			<< "the fast path and the general parser disagree on " << src;
	}
}

/* The specific answers, so a regression says which rule broke rather than
   only that the two paths parted company. */
TEST(YamlJsonFastPath, AQuotedScalarStaysAString) {
	struct Case { const char *src; const char *json; };
	const Case cases[] = {
		{ "[\"\"]",        "[\"\"]" },
		{ "[\"0x10\"]",    "[\"0x10\"]" },
		{ "[\"12\"]",      "[\"12\"]" },
		{ "[\"null\"]",    "[\"null\"]" },
		{ "{\"\":\"\"}",   "{\"\":\"\"}" },
		/* An unquoted JSON number is still a number. */
		{ "[12]",          "[12]" },
		{ "[null]",        "[null]" },
	};
	for (const Case &c : cases) {
		EXPECT_EQ(read_with(c.src, true), std::string(c.json)) << c.src;
	}
}
