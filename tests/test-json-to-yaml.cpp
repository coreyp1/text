/**
 * @file
 *
 * JSON to YAML.
 *
 * YAML 1.2 section 10.2 makes JSON a subset of YAML, so this conversion cannot
 * fail on the grammar - which means the tests are almost entirely about *types*.
 * YAML resolves a plain scalar by its contents, so the interesting cases are the
 * JSON strings whose text reads like something else: a string "true" must not
 * arrive as a boolean, and a string "42" must not arrive as an integer.
 *
 * Most of these are round trips rather than byte comparisons. What matters is
 * that the value survives, and the writer's choice of spelling is its own
 * business; asserting the bytes would pin the writer instead of the conversion.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <gtest/gtest.h>

#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>

namespace {

// Convert JSON text to a YAML document, requiring both steps to succeed.
GTEXT_YAML_Document * to_yaml(const std::string & json_text,
    const GTEXT_YAML_Parse_Options * opts = nullptr) {
	GTEXT_JSON_Error jerr;
	std::memset(&jerr, 0, sizeof(jerr));
	GTEXT_JSON_Value * j =
	    gtext_json_parse(json_text.data(), json_text.size(), nullptr, &jerr);
	EXPECT_NE(j, nullptr) << "the JSON fixture itself did not parse: "
	                      << (jerr.message ? jerr.message : "");
	gtext_json_error_free(&jerr);
	if (!j) {
		return nullptr;
	}

	GTEXT_YAML_Document * doc = nullptr;
	GTEXT_YAML_Error yerr;
	std::memset(&yerr, 0, sizeof(yerr));
	GTEXT_YAML_Status st = gtext_json_to_yaml(j, opts, &doc, &yerr);
	EXPECT_EQ(st, GTEXT_YAML_OK) << (yerr.message ? yerr.message : "");
	gtext_yaml_error_free(&yerr);
	gtext_json_free(j);
	return doc;
}

// Write a YAML document out, so the round trip can be completed.
std::string write_yaml(GTEXT_YAML_Document * doc) {
	GTEXT_YAML_Sink sink;
	std::memset(&sink, 0, sizeof(sink));
	EXPECT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	EXPECT_EQ(gtext_yaml_write_document(doc, &sink, &wopts), GTEXT_YAML_OK);
	std::string out(gtext_yaml_sink_buffer_data(&sink),
	    gtext_yaml_sink_buffer_size(&sink));
	gtext_yaml_sink_buffer_free(&sink);
	return out;
}

// One deterministic spelling of a JSON value, so two of them can be compared.
// Keys sorted, because the comparison is about values and not about order here;
// order has a test of its own.
std::string serialise(const GTEXT_JSON_Value * v) {
	GTEXT_JSON_Sink sink;
	std::memset(&sink, 0, sizeof(sink));
	EXPECT_EQ(gtext_json_sink_buffer(&sink), GTEXT_JSON_OK);
	GTEXT_JSON_Write_Options w = gtext_json_write_options_default();
	w.sort_object_keys = true;
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_json_write_value(&sink, &w, v, &err), GTEXT_JSON_OK)
	    << (err.message ? err.message : "");
	gtext_json_error_free(&err);
	std::string out(gtext_json_sink_buffer_data(&sink),
	    gtext_json_sink_buffer_size(&sink));
	gtext_json_sink_buffer_free(&sink);
	return out;
}

// JSON -> YAML -> JSON, as one deterministic spelling both times.
std::string canonical(const std::string & json_text) {
	GTEXT_JSON_Value * v =
	    gtext_json_parse(json_text.data(), json_text.size(), nullptr, nullptr);
	EXPECT_NE(v, nullptr) << json_text;
	if (!v) {
		return "<unparsed>";
	}
	std::string s = serialise(v);
	gtext_json_free(v);
	return s;
}

std::string round_trip(const std::string & json_text) {
	GTEXT_YAML_Document * doc = to_yaml(json_text);
	if (!doc) {
		return "<no document>";
	}
	GTEXT_JSON_Value * back = nullptr;
	GTEXT_YAML_Error err;
	std::memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_yaml_to_json(doc, &back, &err), GTEXT_YAML_OK)
	    << (err.message ? err.message : "");
	gtext_yaml_error_free(&err);
	gtext_yaml_free(doc);
	if (!back) {
		return "<no json>";
	}
	std::string s = serialise(back);
	gtext_json_free(back);
	return s;
}

} // namespace

TEST(JsonToYaml, EveryJsonShapeConverts) {
	const char * cases[] = {
	    "null",
	    "true",
	    "false",
	    "0",
	    "-1",
	    "\"text\"",
	    "[]",
	    "{}",
	    "[1,2,3]",
	    "{\"a\":1}",
	    "{\"a\":[1,{\"b\":null}],\"c\":true}",
	    "[[[[1]]]]",
	    "{\"a\":{\"b\":{\"c\":{}}}}",
	    "[null,true,false,0,\"\",[],{}]",
	};
	for (const char * src : cases) {
		EXPECT_EQ(round_trip(src), canonical(src))
		    << "round trip changed the value: " << src;
	}
}

/*
 * The case the whole design is for. Each of these is a JSON *string* whose text
 * reads as some other YAML type. Written as a plain YAML scalar, every one would
 * come back as that other type; typed as a string, each survives.
 */
TEST(JsonToYaml, StringsThatLookLikeOtherTypesStayStrings) {
	const char * dangerous[] = {
	    "true", "false", "True", "TRUE", "yes", "no", "on", "off",
	    "null", "Null", "NULL", "~", "",
	    "42", "-17", "0o17", "0x1F", "1.5", "1e3", ".inf", "-.inf", ".nan",
	    "2026-09-23", "12:34", "1:2:3",
	    "- not a sequence", "key: not a mapping", "#not a comment",
	    "*not an alias", "&not an anchor", "!not a tag", "%not a directive",
	    "[1,2]", "{a: 1}", "  leading space", "trailing space  ",
	};
	for (const char * text : dangerous) {
		// Build {"v": "<text>"} as JSON, so the fixture itself is unambiguous.
		GTEXT_JSON_Value * obj = gtext_json_new_object();
		ASSERT_NE(obj, nullptr);
		GTEXT_JSON_Value * str = gtext_json_new_string(text, std::strlen(text));
		ASSERT_NE(str, nullptr);
		ASSERT_EQ(gtext_json_object_put(obj, "v", 1, str), GTEXT_JSON_OK);

		GTEXT_YAML_Document * doc = nullptr;
		GTEXT_YAML_Error err;
		std::memset(&err, 0, sizeof(err));
		ASSERT_EQ(gtext_json_to_yaml(obj, nullptr, &doc, &err), GTEXT_YAML_OK)
		    << (err.message ? err.message : "");
		gtext_yaml_error_free(&err);
		ASSERT_NE(doc, nullptr);

		// The node is a string in the DOM...
		const GTEXT_YAML_Node * root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr) << text;
		const GTEXT_YAML_Node * v =
		    gtext_yaml_mapping_get(root, "v");
		ASSERT_NE(v, nullptr) << text;
		EXPECT_EQ(gtext_yaml_node_type(v), GTEXT_YAML_STRING)
		    << "[" << text << "] did not stay a string in the DOM";

		// ...and it is still a string after being written and read back, which
		// is the half a DOM check alone cannot see: the writer has to quote it.
		std::string written = write_yaml(doc);
		gtext_yaml_free(doc);

		GTEXT_YAML_Document * reread = gtext_yaml_parse(
		    written.data(), written.size(), nullptr, &err);
		ASSERT_NE(reread, nullptr)
		    << "[" << text << "] produced YAML that does not parse: " << written
		    << " / " << (err.message ? err.message : "");
		gtext_yaml_error_free(&err);
		const GTEXT_YAML_Node * rroot = gtext_yaml_document_root(reread);
		ASSERT_NE(rroot, nullptr) << text;
		const GTEXT_YAML_Node * rv = gtext_yaml_mapping_get(rroot, "v");
		ASSERT_NE(rv, nullptr) << text << " in " << written;
		EXPECT_EQ(gtext_yaml_node_type(rv), GTEXT_YAML_STRING)
		    << "[" << text << "] came back as another type from: " << written;
		const char * got = gtext_yaml_node_as_string(rv);
		size_t got_len = got ? std::strlen(got) : 0;
		ASSERT_NE(got, nullptr) << text;
		EXPECT_EQ(std::string(got, got_len), std::string(text))
		    << "the text changed, via: " << written;
		gtext_yaml_free(reread);

		gtext_json_free(obj);
	}
}

/* Object names are strings too, for the same reason. A key of "true" must come
   back as the text `true` rather than as a boolean key. */
TEST(JsonToYaml, ObjectNamesThatLookLikeOtherTypesStayStrings) {
	const char * keys[] = {"true", "null", "42", "1.5", "~", "yes", "on"};
	for (const char * key : keys) {
		GTEXT_JSON_Value * obj = gtext_json_new_object();
		ASSERT_NE(obj, nullptr);
		GTEXT_JSON_Value * one = gtext_json_new_string("x", 1);
		ASSERT_NE(one, nullptr);
		ASSERT_EQ(
		    gtext_json_object_put(obj, key, std::strlen(key), one),
		    GTEXT_JSON_OK);

		GTEXT_YAML_Document * doc = nullptr;
		ASSERT_EQ(gtext_json_to_yaml(obj, nullptr, &doc, nullptr),
		    GTEXT_YAML_OK);
		std::string written = write_yaml(doc);
		gtext_yaml_free(doc);

		GTEXT_YAML_Document * reread =
		    gtext_yaml_parse(written.data(), written.size(), nullptr, nullptr);
		ASSERT_NE(reread, nullptr) << key << " via " << written;
		const GTEXT_YAML_Node * root = gtext_yaml_document_root(reread);
		const GTEXT_YAML_Node * v =
		    gtext_yaml_mapping_get(root, key);
		EXPECT_NE(v, nullptr)
		    << "the key [" << key << "] was not found again, via: " << written;
		gtext_yaml_free(reread);
		gtext_json_free(obj);
	}
}

/*
 * Numbers keep the lexeme the JSON parser preserved. Going through a double
 * would turn 1.0 into 1 and would not represent the last one at all.
 */
TEST(JsonToYaml, NumbersKeepTheirLexeme) {
	struct Case {
		const char * json;
		const char * expect;
		GTEXT_YAML_Node_Type type;
	} cases[] = {
	    {"1", "1", GTEXT_YAML_INT},
	    {"-0", "-0", GTEXT_YAML_INT},
	    {"1.0", "1.0", GTEXT_YAML_FLOAT},
	    {"1.500", "1.500", GTEXT_YAML_FLOAT},
	    {"1e3", "1e3", GTEXT_YAML_FLOAT},
	    {"1E+3", "1E+3", GTEXT_YAML_FLOAT},
	    {"-2.5e-8", "-2.5e-8", GTEXT_YAML_FLOAT},
	    // Too large for an int64, so it has no YAML integer node. It becomes a
	    // string - which is what this library's own YAML parser does with the
	    // same digits, checked in the test below rather than assumed.
	    {"123456789012345678901234567890", "123456789012345678901234567890",
	        GTEXT_YAML_STRING},
	};
	for (const Case & c : cases) {
		std::string doc_text = std::string("{\"v\":") + c.json + "}";
		GTEXT_YAML_Document * doc = to_yaml(doc_text);
		ASSERT_NE(doc, nullptr) << c.json;
		const GTEXT_YAML_Node * root = gtext_yaml_document_root(doc);
		const GTEXT_YAML_Node * v = gtext_yaml_mapping_get(root, "v");
		ASSERT_NE(v, nullptr) << c.json;
		EXPECT_EQ(gtext_yaml_node_type(v), c.type) << c.json;
		const char * text = gtext_yaml_node_as_string(v);
		size_t len = text ? std::strlen(text) : 0;
		ASSERT_NE(text, nullptr) << c.json;
		EXPECT_EQ(std::string(text, len), std::string(c.expect))
		    << "the lexeme was reformatted: " << c.json;
		gtext_yaml_free(doc);
	}
}

/* Order is the document's, not the stack's. A walk that pushed children in
   forward order would reverse every array and object. */
TEST(JsonToYaml, OrderIsPreserved) {
	GTEXT_YAML_Document * doc = to_yaml("[\"a\",\"b\",\"c\",\"d\"]");
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node * root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	ASSERT_EQ(gtext_yaml_sequence_length(root), 4u);
	const char * expect[4] = {"a", "b", "c", "d"};
	for (size_t i = 0; i < 4; i++) {
		const GTEXT_YAML_Node * item = gtext_yaml_sequence_get(root, i);
		ASSERT_NE(item, nullptr) << i;
		const char * text = gtext_yaml_node_as_string(item);
		size_t len = text ? std::strlen(text) : 0;
		ASSERT_NE(text, nullptr) << i;
		EXPECT_EQ(std::string(text, len), std::string(expect[i])) << i;
	}
	gtext_yaml_free(doc);

	// And for an object, where the same mistake would reverse the members.
	GTEXT_YAML_Document * obj =
	    to_yaml("{\"first\":1,\"second\":2,\"third\":3}");
	ASSERT_NE(obj, nullptr);
	std::string written = write_yaml(obj);
	gtext_yaml_free(obj);
	size_t p1 = written.find("first");
	size_t p2 = written.find("second");
	size_t p3 = written.find("third");
	ASSERT_NE(p1, std::string::npos) << written;
	ASSERT_NE(p2, std::string::npos) << written;
	ASSERT_NE(p3, std::string::npos) << written;
	EXPECT_LT(p1, p2) << "members came out reversed: " << written;
	EXPECT_LT(p2, p3) << written;
}

/* Depth is bounded by the option rather than by the C stack, which is what an
   iterative walk buys: a document too deep is a refusal and not a crash. */
TEST(JsonToYaml, DepthIsRefusedRatherThanCrashing) {
	std::string deep;
	const int levels = 5000;
	for (int i = 0; i < levels; i++) {
		deep += "[";
	}
	deep += "1";
	for (int i = 0; i < levels; i++) {
		deep += "]";
	}

	GTEXT_JSON_Parse_Options jopts = gtext_json_parse_options_default();
	jopts.max_depth = levels + 10;
	GTEXT_JSON_Value * j =
	    gtext_json_parse(deep.data(), deep.size(), &jopts, nullptr);
	ASSERT_NE(j, nullptr) << "the JSON fixture must parse for this to test "
	                         "anything";

	GTEXT_YAML_Parse_Options yopts = gtext_yaml_parse_options_default();
	yopts.max_depth = 64;
	GTEXT_YAML_Document * doc = nullptr;
	GTEXT_YAML_Error err;
	std::memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_json_to_yaml(j, &yopts, &doc, &err), GTEXT_YAML_E_DEPTH);
	EXPECT_EQ(doc, nullptr) << "nothing may be handed back on a refusal";
	ASSERT_NE(err.message, nullptr);
	EXPECT_NE(std::string(err.message).find("max_depth"), std::string::npos)
	    << err.message;
	gtext_yaml_error_free(&err);

	// And with room, the same document converts.
	yopts.max_depth = levels + 10;
	std::memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_json_to_yaml(j, &yopts, &doc, &err), GTEXT_YAML_OK)
	    << (err.message ? err.message : "");
	EXPECT_NE(doc, nullptr);
	if (doc) {
		gtext_yaml_free(doc);
	}
	gtext_yaml_error_free(&err);
	gtext_json_free(j);
}

TEST(JsonToYaml, NullArgumentsAreRefused) {
	GTEXT_YAML_Document * doc = nullptr;
	EXPECT_EQ(gtext_json_to_yaml(nullptr, nullptr, &doc, nullptr),
	    GTEXT_YAML_E_INVALID);
	GTEXT_JSON_Value * j = gtext_json_new_object();
	ASSERT_NE(j, nullptr);
	EXPECT_EQ(gtext_json_to_yaml(j, nullptr, nullptr, nullptr),
	    GTEXT_YAML_E_INVALID);
	gtext_json_free(j);
}

/* UTF-8 passes through untouched, including the characters a writer might be
   tempted to escape. */
TEST(JsonToYaml, Utf8SurvivesTheRoundTrip) {
	const char * src =
	    "{\"caf\xc3\xa9\":\"\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89\","
	    "\"emoji\":\"\xf0\x9f\x90\x9b\"}";
	EXPECT_EQ(round_trip(src), canonical(src));
}


/*
 * The one lossy case, and the reason it is the answer it is.
 *
 * A JSON integer too large for an int64 has no YAML integer node here. Rather
 * than refuse the document or invent a spelling, the conversion gives it the
 * same type this library's YAML parser gives the same digits - which this test
 * establishes rather than assumes, by parsing them.
 */
TEST(JsonToYaml, AnOversizedIntegerBecomesWhateverTheYamlParserCallsIt) {
	const char * digits = "123456789012345678901234567890";

	// What the YAML parser makes of those digits.
	std::string yaml_src = std::string("v: ") + digits + "\n";
	GTEXT_YAML_Document * parsed =
	    gtext_yaml_parse(yaml_src.data(), yaml_src.size(), nullptr, nullptr);
	ASSERT_NE(parsed, nullptr);
	const GTEXT_YAML_Node * pv =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(parsed), "v");
	ASSERT_NE(pv, nullptr);
	const GTEXT_YAML_Node_Type parser_says = gtext_yaml_node_type(pv);
	gtext_yaml_free(parsed);

	// What the conversion makes of the same digits.
	std::string json_src = std::string("{\"v\":") + digits + "}";
	GTEXT_YAML_Document * converted = to_yaml(json_src);
	ASSERT_NE(converted, nullptr);
	const GTEXT_YAML_Node * cv =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(converted), "v");
	ASSERT_NE(cv, nullptr);

	EXPECT_EQ(gtext_yaml_node_type(cv), parser_says)
	    << "the conversion and the parser disagree about the same digits";
	const char * text = gtext_yaml_node_as_string(cv);
	ASSERT_NE(text, nullptr);
	EXPECT_EQ(std::string(text), std::string(digits))
	    << "the digits themselves must survive even though the type does not";

	// And what it produces still reads back as what it says.
	std::string written = write_yaml(converted);
	gtext_yaml_free(converted);
	GTEXT_YAML_Document * reread =
	    gtext_yaml_parse(written.data(), written.size(), nullptr, nullptr);
	ASSERT_NE(reread, nullptr) << written;
	const GTEXT_YAML_Node * rv =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(reread), "v");
	ASSERT_NE(rv, nullptr) << written;
	EXPECT_EQ(gtext_yaml_node_type(rv), parser_says) << written;
	EXPECT_EQ(std::string(gtext_yaml_node_as_string(rv)), std::string(digits))
	    << written;
	gtext_yaml_free(reread);
}
