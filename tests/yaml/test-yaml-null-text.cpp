/**
 * What a null node's text says, and what a null key becomes in JSON.
 *
 * The empty node resolves to null (7.2), and the parser used to supply it as
 * the scalar "~" rather than as an empty one. That was a workaround: the
 * resolver read a scalar's text without looking at how it was written, so an
 * empty scalar resolved to the empty *string*, and "a:" could not be told
 * apart from "a: ''". The resolver consults the style now - only a plain
 * scalar is resolved by its contents (10.3.2) - so a plain empty scalar
 * reaches null on its own and the substitute is no longer needed.
 *
 * The substitute was visible. gtext_yaml_node_as_string() returned "~" for a
 * node whose author wrote nothing, and not everywhere: the paths that supply
 * a missing value, a missing entry and a flow mapping's missing key used it,
 * while the one that builds an empty node out of properties alone did not.
 * So two null keys written the same way - "? " over ": 1" and "&y : 1" -
 * carried different text and became different JSON names once coerced.
 *
 * The rule now is that the text is what the author wrote: nothing for a node
 * nobody wrote, "~" for a node spelled "~", "null" for one spelled "null".
 * The type is null in all three cases and always was.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>

namespace {

/* The first pair's key or value, or the first sequence entry, or the root. */
const GTEXT_YAML_Node *FirstOf(
		const GTEXT_YAML_Document *doc, bool want_key) {
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	if (!root) return nullptr;
	if (gtext_yaml_node_type(root) == GTEXT_YAML_MAPPING) {
		const GTEXT_YAML_Node *k = nullptr, *v = nullptr;
		gtext_yaml_mapping_get_at(root, 0, &k, &v);
		return want_key ? k : v;
	}
	if (gtext_yaml_node_type(root) == GTEXT_YAML_SEQUENCE) {
		return gtext_yaml_sequence_get(root, 0);
	}
	return root;
}

struct Case {
	const char *input;
	bool key;                        /* look at the key, not the value */
	GTEXT_YAML_Node_Type type;
	const char *text;
};

const Case kCases[] = {
	/* Written as nothing: no text, because none was written. */
	{"a:\n",           false, GTEXT_YAML_NULL,   ""},
	{"{a: }\n",        false, GTEXT_YAML_NULL,   ""},
	{"-\n",            false, GTEXT_YAML_NULL,   ""},
	{"? \n: 1\n",      true,  GTEXT_YAML_NULL,   ""},
	{"{ : 1 }\n",      true,  GTEXT_YAML_NULL,   ""},
	{"&y : 1\n",       true,  GTEXT_YAML_NULL,   ""},
	{"{ &a : 1 }\n",   true,  GTEXT_YAML_NULL,   ""},

	/* Written as something: that something. */
	{"a: ~\n",         false, GTEXT_YAML_NULL,   "~"},
	{"a: null\n",      false, GTEXT_YAML_NULL,   "null"},
	{"a: Null\n",      false, GTEXT_YAML_NULL,   "Null"},
	{"[~]\n",          false, GTEXT_YAML_NULL,   "~"},
	{"~: 1\n",         true,  GTEXT_YAML_NULL,   "~"},

	/* A tag decides the type, and an empty string is a string. */
	{"!!str : 1\n",    true,  GTEXT_YAML_STRING, ""},
	{"a: !!null ''\n", false, GTEXT_YAML_NULL,   ""},

	/* The distinction the "~" substitute existed to protect: an empty plain
	   scalar is null and an empty quoted one is the empty string. */
	{"a: ''\n",        false, GTEXT_YAML_STRING, ""},
	{"a: \"\"\n",      false, GTEXT_YAML_STRING, ""},
	{"'': 1\n",        true,  GTEXT_YAML_STRING, ""},
};

std::string ToJson(const char *src, bool coerce, GTEXT_YAML_Status *status) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), nullptr, &err);
	if (!doc) { *status = GTEXT_YAML_E_INVALID; return ""; }
	GTEXT_JSON_Value *jv = nullptr;
	GTEXT_YAML_To_JSON_Options o = gtext_yaml_to_json_options_default();
	o.coerce_keys_to_strings = coerce;
	memset(&err, 0, sizeof(err));
	*status = gtext_yaml_to_json_with_options(doc, &jv, &o, &err);
	if (*status != GTEXT_YAML_OK) { gtext_yaml_free(doc); return ""; }
	GTEXT_JSON_Sink sink;
	gtext_json_sink_buffer(&sink);
	GTEXT_JSON_Error je;
	memset(&je, 0, sizeof(je));
	gtext_json_write_value(&sink, nullptr, jv, &je);
	std::string out(gtext_json_sink_buffer_data(&sink),
			gtext_json_sink_buffer_size(&sink));
	gtext_json_sink_buffer_free(&sink);
	gtext_json_free(jv);
	gtext_yaml_free(doc);
	return out;
}

}  // namespace

TEST(YamlNullText, TheTextIsWhatWasWritten) {
	for (const Case &c : kCases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.input, strlen(c.input), nullptr, &err);
		ASSERT_NE(doc, nullptr)
			<< c.input << ": " << (err.message ? err.message : "?");
		const GTEXT_YAML_Node *n = FirstOf(doc, c.key);
		ASSERT_NE(n, nullptr) << c.input;
		EXPECT_EQ(gtext_yaml_node_type(n), c.type) << "input: " << c.input;
		const char *text = gtext_yaml_node_as_string(n);
		ASSERT_NE(text, nullptr) << "input: " << c.input;
		EXPECT_STREQ(text, c.text) << "input: " << c.input;
		gtext_yaml_free(doc);
	}
}

/* Every spelling of an unwritten node agrees with every other. */
TEST(YamlNullText, EveryUnwrittenNodeAgrees) {
	const char *same[] = {
		"a:\n", "{a: }\n", "-\n", "? \n: 1\n", "{ : 1 }\n", "&y : 1\n",
	};
	for (const char *src : same) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(src, strlen(src), nullptr, &err);
		ASSERT_NE(doc, nullptr) << src;
		const GTEXT_YAML_Node *n =
			FirstOf(doc, strstr(src, ": 1") != nullptr);
		ASSERT_NE(n, nullptr) << src;
		EXPECT_EQ(gtext_yaml_node_type(n), GTEXT_YAML_NULL) << src;
		EXPECT_STREQ(gtext_yaml_node_as_string(n), "") << src;
		gtext_yaml_free(doc);
	}
}

/* The writer spells an empty null node "~", so nothing is lost by storing no
   text for it: what comes back is null again. */
TEST(YamlNullText, AnEmptyNullNodeStillRoundTrips) {
	struct { const char *input; GTEXT_YAML_Node_Type type; } cases[] = {
		{"a:\n", GTEXT_YAML_NULL},
		{"a: ~\n", GTEXT_YAML_NULL},
		{"a: null\n", GTEXT_YAML_NULL},
		{"a: ''\n", GTEXT_YAML_STRING},
	};
	for (const auto &c : cases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.input, strlen(c.input), nullptr, &err);
		ASSERT_NE(doc, nullptr) << c.input;
		GTEXT_YAML_Sink sink;
		ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
		GTEXT_YAML_Write_Options wo = gtext_yaml_write_options_default();
		ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &wo), GTEXT_YAML_OK);
		const std::string written(gtext_yaml_sink_buffer_data(&sink),
				gtext_yaml_sink_buffer_size(&sink));
		gtext_yaml_sink_buffer_free(&sink);
		gtext_yaml_free(doc);

		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *back =
			gtext_yaml_parse(written.c_str(), written.size(), nullptr, &err);
		ASSERT_NE(back, nullptr) << written;
		const GTEXT_YAML_Node *v = FirstOf(back, false);
		ASSERT_NE(v, nullptr) << written;
		EXPECT_EQ(gtext_yaml_node_type(v), c.type)
			<< c.input << " wrote " << written;
		gtext_yaml_free(back);
	}
}

/* Coercing a key to a string is many-to-one, and two YAML keys that land on
   one JSON name used to leave the object holding whichever came last. */
TEST(YamlNullText, CoercionCollisionsAreRefused) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	const char *collide[] = {
		"? \n: 1\n'': 2\n",     /* the null key and the empty string key */
		"'': 1\n? \n: 2\n",     /* ...in the other order */
		"1: a\n'1': b\n",       /* the integer 1 and the string "1" */
		"true: a\n'true': b\n", /* the boolean and the string */
	};
	for (const char *src : collide) {
		ToJson(src, true, &status);
		EXPECT_EQ(status, GTEXT_YAML_E_INVALID) << "accepted: " << src;
	}
}

TEST(YamlNullText, KeysThatDoNotCollideStillConvert) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	struct { const char *input; const char *json; } cases[] = {
		{"a: 1\nb: 2\n", "{\"a\":1,\"b\":2}"},
		{"? \n: 1\n", "{\"\":1}"},
		{"~: 1\n", "{\"~\":1}"},
		{"null: 1\n", "{\"null\":1}"},
		{"1: a\n2: b\n", "{\"1\":\"a\",\"2\":\"b\"}"},
	};
	for (const auto &c : cases) {
		EXPECT_EQ(ToJson(c.input, true, &status), c.json) << c.input;
		EXPECT_EQ(status, GTEXT_YAML_OK) << c.input;
	}
}

/* !!pairs and !!omap become JSON arrays, so their duplicate keys are not the
   converter's to refuse, and a dupkeys policy that resolves a duplicate does
   so before the conversion sees it. */
/* The JSON name a key coerces to is the key as it was written, so "~" and a
   key nobody wrote produce different names - but they are the same YAML key,
   both null, so no document can hold the two and the difference is never
   visible inside one object. The duplicate-key policy catches it first. */
TEST(YamlNullText, TwoSpellingsOfNullAreOneKey) {
	const char *src = "~: 1\n? \n: 2\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(src, strlen(src), nullptr, &err);
	EXPECT_EQ(doc, nullptr) << "two null keys should be duplicates";
	if (doc) gtext_yaml_free(doc);
	else EXPECT_STREQ(err.message, "Duplicate mapping key");
}

TEST(YamlNullText, TheCollisionCheckLeavesEverythingElseAlone) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	EXPECT_EQ(ToJson("!!pairs [{a: 1}, {a: 2}]\n", true, &status),
			"[{\"a\":1},{\"a\":2}]");
	EXPECT_EQ(status, GTEXT_YAML_OK);
	EXPECT_EQ(ToJson("!!omap [{a: 1}, {b: 2}]\n", true, &status),
			"[{\"a\":1},{\"b\":2}]");
	EXPECT_EQ(status, GTEXT_YAML_OK);

	for (GTEXT_YAML_Dupkey_Mode mode :
			{GTEXT_YAML_DUPKEY_LAST_WINS, GTEXT_YAML_DUPKEY_FIRST_WINS}) {
		GTEXT_YAML_Parse_Options po = gtext_yaml_parse_options_default();
		po.dupkeys = mode;
		const char *src = "a: 1\na: 2\n";
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(src, strlen(src), &po, &err);
		ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
		GTEXT_JSON_Value *jv = nullptr;
		GTEXT_YAML_To_JSON_Options o = gtext_yaml_to_json_options_default();
		o.coerce_keys_to_strings = true;
		memset(&err, 0, sizeof(err));
		EXPECT_EQ(gtext_yaml_to_json_with_options(doc, &jv, &o, &err),
				GTEXT_YAML_OK)
			<< "mode " << (int)mode << ": "
			<< (err.message ? err.message : "?");
		if (jv) gtext_json_free(jv);
		gtext_yaml_free(doc);
	}
}
