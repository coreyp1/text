/**
 * The JSON name a non-string mapping key coerces to.
 *
 * coerce_keys_to_strings took the key *as written*. That contradicted the
 * parser's own idea of which keys are the same: mapping key identity here is
 * (type, value) and not spelling, so "0x10" and "16" are one key and are
 * refused as duplicates, as are "0o14" and "12", "Null" and "~", and "True"
 * and "true". The conversion then emitted a different name for each, so two
 * YAML documents that mean the same thing became two different JSON
 * documents:
 *
 *     0x10: a   ->  {"0x10": "a"}        the same key, converted twice,
 *     16: a     ->  {"16": "a"}          two different ways
 *
 * Both rules are coherent on their own; this one loses because the
 * conversion is not a round trip. Anchors, tags, comments and styles are
 * already gone by the time a key is named, and a JSON name is a string, so
 * there is nothing for the spelling to be faithful to. Number values keep
 * their lexeme where there IS a round trip to protect - JSON in, identical
 * JSON out - which is a different operation with a different guarantee.
 *
 * Coercion stays many-to-one even by value: the integer 1 and the string "1"
 * are different keys and JSON has one name for them. Those still collide and
 * are still refused rather than silently merged.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>

namespace {

std::string ToJson(const char *src, GTEXT_YAML_Status *status,
		const GTEXT_YAML_Parse_Options *po = nullptr) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), po, &err);
	if (!doc) { *status = GTEXT_YAML_E_INVALID; return ""; }
	GTEXT_JSON_Value *jv = nullptr;
	GTEXT_YAML_To_JSON_Options o = gtext_yaml_to_json_options_default();
	o.coerce_keys_to_strings = true;
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

/* Every pair here is one key written two ways, and each pair is refused as a
   duplicate when both spellings appear in one mapping - which is what makes
   them the same key rather than merely similar. */
TEST(YamlKeyCoercion, OneKeyGetsOneNameHoweverItIsWritten) {
	struct { const char *a; const char *b; const char *name; } pairs[] = {
		{"0x10: v\n",  "16: v\n",    "16"},
		{"0o14: v\n",  "12: v\n",    "12"},
		{"+1: v\n",    "1: v\n",     "1"},
		{"-0: v\n",    "0: v\n",     "0"},
		{"Null: v\n",  "~: v\n",     "null"},
		{"null: v\n",  "? \n: v\n",  "null"},
		{"True: v\n",  "true: v\n",  "true"},
		{"False: v\n", "false: v\n", "false"},
		{"1.0: v\n",   "1.00: v\n",  "1.0"},
	};
	for (const auto &p : pairs) {
		GTEXT_YAML_Status sa = GTEXT_YAML_OK, sb = GTEXT_YAML_OK;
		const std::string want = std::string("{\"") + p.name + "\":\"v\"}";
		EXPECT_EQ(ToJson(p.a, &sa), want) << p.a;
		EXPECT_EQ(ToJson(p.b, &sb), want) << p.b;
		EXPECT_EQ(sa, GTEXT_YAML_OK) << p.a;
		EXPECT_EQ(sb, GTEXT_YAML_OK) << p.b;

		/* ...and the two together are one key twice over. */
		const std::string both = std::string(p.a) + p.b;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(both.c_str(), both.size(), nullptr, &err);
		EXPECT_EQ(doc, nullptr) << "not duplicates: " << both;
		if (doc) gtext_yaml_free(doc);
	}
}

/* A float name reads back as the same double and always looks like a float,
   so it can never be mistaken for, or collide with, an integer key. */
TEST(YamlKeyCoercion, FloatNamesAreTheShortestThatRoundTrips) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	struct { const char *input; const char *json; } cases[] = {
		{"0.1: v\n",                "{\"0.1\":\"v\"}"},
		{"1.0: v\n",                "{\"1.0\":\"v\"}"},
		{"1e3: v\n",                "{\"1000.0\":\"v\"}"},
		{"1e20: v\n",               "{\"1e+20\":\"v\"}"},
		{"3.141592653589793: v\n",  "{\"3.141592653589793\":\"v\"}"},
		{"0.30000000000000004: v\n","{\"0.30000000000000004\":\"v\"}"},
		{"-2.5: v\n",               "{\"-2.5\":\"v\"}"},
	};
	for (const auto &c : cases) {
		EXPECT_EQ(ToJson(c.input, &status), c.json) << c.input;
		EXPECT_EQ(status, GTEXT_YAML_OK) << c.input;
	}
}

/* ".inf" and ".nan" are floats with no JSON spelling, which is why the
   writer already refuses them as values. */
TEST(YamlKeyCoercion, NonFiniteFloatKeysAreRefused) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	for (const char *src : {".inf: v\n", "-.inf: v\n", ".nan: v\n"}) {
		ToJson(src, &status);
		EXPECT_EQ(status, GTEXT_YAML_E_INVALID) << "accepted: " << src;
	}
}

/* An integer too large for int64, or one written with leading zeros, is not
   an integer in the core schema at all - it stays a string and is its own
   name. */
TEST(YamlKeyCoercion, WhatTheCoreSchemaLeavesAStringStaysAString) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	EXPECT_EQ(ToJson("99999999999999999999999: v\n", &status),
			"{\"99999999999999999999999\":\"v\"}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
	EXPECT_EQ(ToJson("00123: v\n", &status), "{\"00123\":\"v\"}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
	/* "yes" is a string in the 1.2 core schema, not a boolean. */
	EXPECT_EQ(ToJson("yes: v\n", &status), "{\"yes\":\"v\"}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
}

/* Naming by value shrinks the set of collisions but cannot remove it: JSON
   has one name for the integer 1 and the string "1". */
TEST(YamlKeyCoercion, CollisionsThatRemainAreRefusedNotMerged) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	const char *collide[] = {
		"1: a\n'1': b\n",
		"'1': a\n1: b\n",
		"true: a\n'true': b\n",
		"~: a\n'null': b\n",
		"1.0: a\n'1.0': b\n",
	};
	for (const char *src : collide) {
		ToJson(src, &status);
		EXPECT_EQ(status, GTEXT_YAML_E_INVALID) << "accepted: " << src;
	}
}

/* The null key and the empty string key used to collide, because an empty
   node spelled itself "". By value they are plainly different. */
TEST(YamlKeyCoercion, TheNullKeyAndTheEmptyStringKeyAreTwoNames) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	EXPECT_EQ(ToJson("? \n: 1\n'': 2\n", &status), "{\"null\":1,\"\":2}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
}

TEST(YamlKeyCoercion, OrdinaryDocumentsAreUntouched) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	EXPECT_EQ(ToJson("a: 1\nb: 2\n", &status), "{\"a\":1,\"b\":2}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
	EXPECT_EQ(ToJson("1: a\n2: b\n", &status), "{\"1\":\"a\",\"2\":\"b\"}");
	EXPECT_EQ(status, GTEXT_YAML_OK);
}

/* Without the option a non-string key is still refused outright, which is
   the behaviour of the default options. */
TEST(YamlKeyCoercion, WithoutTheOptionANonStringKeyIsRefused) {
	const char *src = "1: a\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), nullptr, &err);
	ASSERT_NE(doc, nullptr);
	GTEXT_JSON_Value *jv = nullptr;
	GTEXT_YAML_To_JSON_Options o = gtext_yaml_to_json_options_default();
	EXPECT_FALSE(o.coerce_keys_to_strings);
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_yaml_to_json_with_options(doc, &jv, &o, &err),
			GTEXT_YAML_E_INVALID);
	if (jv) gtext_json_free(jv);
	gtext_yaml_free(doc);
}

/* !!pairs and !!omap become JSON arrays, so their duplicate keys never reach
   the object path, and a dupkeys policy that resolves a duplicate does so in
   the DOM long before the conversion sees it. */
TEST(YamlKeyCoercion, CollectionsAndDupkeyPoliciesAreUnaffected) {
	GTEXT_YAML_Status status = GTEXT_YAML_OK;
	EXPECT_EQ(ToJson("!!pairs [{a: 1}, {a: 2}]\n", &status),
			"[{\"a\":1},{\"a\":2}]");
	EXPECT_EQ(status, GTEXT_YAML_OK);
	EXPECT_EQ(ToJson("!!omap [{a: 1}, {b: 2}]\n", &status),
			"[{\"a\":1},{\"b\":2}]");
	EXPECT_EQ(status, GTEXT_YAML_OK);

	for (GTEXT_YAML_Dupkey_Mode mode :
			{GTEXT_YAML_DUPKEY_LAST_WINS, GTEXT_YAML_DUPKEY_FIRST_WINS}) {
		GTEXT_YAML_Parse_Options po = gtext_yaml_parse_options_default();
		po.dupkeys = mode;
		EXPECT_EQ(ToJson("0x10: 1\n16: 2\n", &status, &po).empty(), false)
			<< "mode " << (int)mode;
		EXPECT_EQ(status, GTEXT_YAML_OK) << "mode " << (int)mode;
	}
}
