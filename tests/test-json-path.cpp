/**
 * @file
 *
 * JSONPath (RFC 9535), without the filter selector.
 *
 * The queries in the first half come from the specification: the bookstore
 * document of section 1.5 with its table of examples, and the slice examples of
 * 2.3.4. Writing the RFC's own answers down is what makes this a test of the
 * specification rather than of the implementation - a table built by running the
 * code would agree with whatever the code does.
 *
 * Results are compared as a rendering of the selected values, in order, because
 * order is part of what RFC 9535 specifies: a node list is a list, `$[0,0]`
 * selects the same node twice, and a descendant segment visits in document
 * order.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include <ghoti.io/text/json.h>

namespace {

// The specification's example document, section 1.5.
const char * const kStore = R"({ "store": {
    "book": [
      { "category": "reference",
        "author": "Nigel Rees",
        "title": "Sayings of the Century",
        "price": 8.95
      },
      { "category": "fiction",
        "author": "Evelyn Waugh",
        "title": "Sword of Honour",
        "price": 12.99
      },
      { "category": "fiction",
        "author": "Herman Melville",
        "title": "Moby Dick",
        "isbn": "0-553-21311-3",
        "price": 8.99
      },
      { "category": "fiction",
        "author": "J. R. R. Tolkien",
        "title": "The Lord of the Rings",
        "isbn": "0-395-19395-8",
        "price": 22.99
      }
    ],
    "bicycle": {
      "color": "red",
      "price": 399
    }
  }
})";

// One value, rendered compactly enough to compare in a table.
std::string render(const GTEXT_JSON_Value * v) {
	if (!v) {
		return "<null pointer>";
	}
	switch (gtext_json_typeof(v)) {
	case GTEXT_JSON_NULL:
		return "null";
	case GTEXT_JSON_BOOL: {
		bool b = false;
		gtext_json_get_bool(v, &b);
		return b ? "true" : "false";
	}
	case GTEXT_JSON_NUMBER: {
		const char * lexeme = nullptr;
		size_t len = 0;
		if (gtext_json_get_number_lexeme(v, &lexeme, &len) == GTEXT_JSON_OK) {
			return std::string(lexeme, len);
		}
		double d = 0.0;
		gtext_json_get_double(v, &d);
		return std::to_string(d);
	}
	case GTEXT_JSON_STRING: {
		const char * s = nullptr;
		size_t len = 0;
		gtext_json_get_string(v, &s, &len);
		return "\"" + std::string(s, len) + "\"";
	}
	case GTEXT_JSON_ARRAY: {
		std::string out = "[";
		const size_t n = gtext_json_array_size(v);
		for (size_t i = 0; i < n; i++) {
			if (i) {
				out += ",";
			}
			out += render(gtext_json_array_get(v, i));
		}
		return out + "]";
	}
	case GTEXT_JSON_OBJECT: {
		std::string out = "{";
		const size_t n = gtext_json_object_size(v);
		for (size_t i = 0; i < n; i++) {
			if (i) {
				out += ",";
			}
			size_t klen = 0;
			const char * k = gtext_json_object_key(v, i, &klen);
			out += "\"" + std::string(k ? k : "", klen) + "\":";
			out += render(gtext_json_object_value(v, i));
		}
		return out + "}";
	}
	}
	return "?";
}

struct Parsed {
	GTEXT_JSON_Value * doc = nullptr;
	~Parsed() {
		if (doc) {
			gtext_json_free(doc);
		}
	}
};

GTEXT_JSON_Value * parse(const char * text) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.preserve_number_lexeme = true;
	return gtext_json_parse(text, std::strlen(text), &opts, nullptr);
}

// The rendered results of a query, joined with "|", or "!" and the status on
// failure - so a table can carry both answers and refusals.
std::string select(const GTEXT_JSON_Value * root, const char * query) {
	GTEXT_JSON_Path_Result result;
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	const GTEXT_JSON_Status status = gtext_json_path_query(
	    root, query, SIZE_MAX, nullptr, &result, &err);
	if (status != GTEXT_JSON_OK) {
		return "!" + std::to_string((int)status);
	}
	std::string out;
	for (size_t i = 0; i < result.count; i++) {
		if (i) {
			out += "|";
		}
		out += render(result.nodes[i]);
	}
	gtext_json_path_result_free(&result);
	return out;
}

size_t count(const GTEXT_JSON_Value * root, const char * query) {
	GTEXT_JSON_Path_Result result;
	if (gtext_json_path_query(root, query, SIZE_MAX, nullptr, &result, nullptr)
	    != GTEXT_JSON_OK) {
		return SIZE_MAX;
	}
	const size_t n = result.count;
	gtext_json_path_result_free(&result);
	return n;
}

} // namespace

// ===========================================================================
// The specification's own examples
// ===========================================================================

TEST(JsonPath, TheTableFromSectionOnePointFive) {
	Parsed p;
	p.doc = parse(kStore);
	ASSERT_NE(p.doc, nullptr);

	// The authors of all books in the store.
	EXPECT_EQ(select(p.doc, "$.store.book[*].author"),
	    "\"Nigel Rees\"|\"Evelyn Waugh\"|\"Herman Melville\"|"
	    "\"J. R. R. Tolkien\"");
	// All authors.
	EXPECT_EQ(select(p.doc, "$..author"),
	    "\"Nigel Rees\"|\"Evelyn Waugh\"|\"Herman Melville\"|"
	    "\"J. R. R. Tolkien\"");
	// All things in the store: the book array and the bicycle.
	EXPECT_EQ(count(p.doc, "$.store.*"), 2u);
	// The prices of everything in the store.
	EXPECT_EQ(select(p.doc, "$.store..price"),
	    "8.95|12.99|8.99|22.99|399");
	// The third book.
	EXPECT_EQ(select(p.doc, "$..book[2].title"), "\"Moby Dick\"");
	// The last book in order.
	EXPECT_EQ(select(p.doc, "$..book[-1].title"),
	    "\"The Lord of the Rings\"");
	// The first two books, two ways.
	EXPECT_EQ(select(p.doc, "$..book[0,1].title"),
	    "\"Sayings of the Century\"|\"Sword of Honour\"");
	EXPECT_EQ(select(p.doc, "$..book[:2].title"),
	    "\"Sayings of the Century\"|\"Sword of Honour\"");
	// All member values and array elements contained in the input value.
	// The document has 1 + 2 + 4 + 4+4+5+5 + 2 = 27 of them.
	EXPECT_EQ(count(p.doc, "$..*"), 27u);
}

/* The bracketed and shorthand spellings of a name are the same selector
   (2.5.1.2), including where the shorthand cannot spell it. */
TEST(JsonPath, BracketedAndShorthandNamesAgree) {
	Parsed p;
	p.doc = parse(R"({"a":1,"b c":2,"d.e":3,"":4,"é":5})");
	ASSERT_NE(p.doc, nullptr);

	EXPECT_EQ(select(p.doc, "$.a"), "1");
	EXPECT_EQ(select(p.doc, "$['a']"), "1");
	EXPECT_EQ(select(p.doc, "$[\"a\"]"), "1");
	// A name with a space or a dot in it has no shorthand.
	EXPECT_EQ(select(p.doc, "$['b c']"), "2");
	EXPECT_EQ(select(p.doc, "$['d.e']"), "3");
	EXPECT_EQ(select(p.doc, "$.d.e"), ""); // reads as two segments: nothing
	// The empty name is a name.
	EXPECT_EQ(select(p.doc, "$['']"), "4");
	// An escape names the character it spells, and the document's own escape
	// spelled the same character.
	EXPECT_EQ(select(p.doc, "$['\\u00e9']"), "5");
	EXPECT_EQ(select(p.doc, "$['\xC3\xA9']"), "5");
	// A name that is not there selects nothing, which is not an error.
	EXPECT_EQ(select(p.doc, "$.missing"), "");
	EXPECT_EQ(select(p.doc, "$.a.b.c.d"), "");
}

TEST(JsonPath, IndexSelector) {
	Parsed p;
	p.doc = parse(R"(["a","b","c","d","e"])");
	ASSERT_NE(p.doc, nullptr);

	EXPECT_EQ(select(p.doc, "$[0]"), "\"a\"");
	EXPECT_EQ(select(p.doc, "$[4]"), "\"e\"");
	EXPECT_EQ(select(p.doc, "$[-1]"), "\"e\"");
	EXPECT_EQ(select(p.doc, "$[-5]"), "\"a\"");
	// Out of range selects nothing.
	EXPECT_EQ(select(p.doc, "$[5]"), "");
	EXPECT_EQ(select(p.doc, "$[-6]"), "");
	// An index applied to an object selects nothing - it is not a name.
	Parsed obj;
	obj.doc = parse(R"({"0":"zero"})");
	ASSERT_NE(obj.doc, nullptr);
	EXPECT_EQ(select(obj.doc, "$[0]"), "");
	EXPECT_EQ(select(obj.doc, "$['0']"), "\"zero\"");
}

/* The slice examples of 2.3.4.3, and the rules that produce them. */
TEST(JsonPath, SliceSelector) {
	Parsed p;
	p.doc = parse(R"(["a","b","c","d","e","f","g"])");
	ASSERT_NE(p.doc, nullptr);

	EXPECT_EQ(select(p.doc, "$[1:3]"), "\"b\"|\"c\"");
	EXPECT_EQ(select(p.doc, "$[5:]"), "\"f\"|\"g\"");
	EXPECT_EQ(select(p.doc, "$[1:5:2]"), "\"b\"|\"d\"");
	EXPECT_EQ(select(p.doc, "$[5:1:-2]"), "\"f\"|\"d\"");
	EXPECT_EQ(select(p.doc, "$[::-1]"),
	    "\"g\"|\"f\"|\"e\"|\"d\"|\"c\"|\"b\"|\"a\"");
	EXPECT_EQ(select(p.doc, "$[:]"),
	    "\"a\"|\"b\"|\"c\"|\"d\"|\"e\"|\"f\"|\"g\"");
	// A step of zero selects nothing, which the specification says rather than
	// leaving to a division.
	EXPECT_EQ(select(p.doc, "$[::0]"), "");
	// Bounds are clamped, not errors.
	EXPECT_EQ(select(p.doc, "$[100:200]"), "");
	EXPECT_EQ(select(p.doc, "$[-100:2]"), "\"a\"|\"b\"");
	// A negative bound counts from the end.
	EXPECT_EQ(select(p.doc, "$[-2:]"), "\"f\"|\"g\"");
	EXPECT_EQ(select(p.doc, "$[:-5]"), "\"a\"|\"b\"");
	// A slice applied to an object selects nothing.
	Parsed obj;
	obj.doc = parse(R"({"a":1,"b":2})");
	ASSERT_NE(obj.doc, nullptr);
	EXPECT_EQ(select(obj.doc, "$[0:2]"), "");
}

TEST(JsonPath, WildcardSelector) {
	Parsed p;
	p.doc = parse(R"({"o":{"j":1,"k":2},"a":[5,3]})");
	ASSERT_NE(p.doc, nullptr);

	// 2.3.2.3's examples.
	EXPECT_EQ(select(p.doc, "$[*]"), "{\"j\":1,\"k\":2}|[5,3]");
	EXPECT_EQ(select(p.doc, "$.o[*]"), "1|2");
	EXPECT_EQ(select(p.doc, "$.o[*, *]"), "1|2|1|2");
	EXPECT_EQ(select(p.doc, "$.a[*]"), "5|3");
	// A wildcard on a scalar selects nothing.
	EXPECT_EQ(select(p.doc, "$.o.j[*]"), "");
}

/* 2.5.2.3: a descendant segment visits the node and everything under it, and
   the order is the document's. */
TEST(JsonPath, DescendantSegment) {
	Parsed p;
	p.doc = parse(R"({"o":{"j":1,"k":2},"a":[5,3,[{"j":4},{"k":6}]]})");
	ASSERT_NE(p.doc, nullptr);

	EXPECT_EQ(select(p.doc, "$..j"), "1|4");
	EXPECT_EQ(select(p.doc, "$..[0]"), "5|{\"j\":4}");
	EXPECT_EQ(select(p.doc, "$..[*]").empty(), false);
	// The descendant of a scalar is the scalar itself, so a name under it
	// selects nothing rather than erroring.
	EXPECT_EQ(select(p.doc, "$.o.j..k"), "");
}

/* A node list is a list: the same node twice if the query says so (2.3.1.2). */
TEST(JsonPath, DuplicatesAreKept) {
	Parsed p;
	p.doc = parse(R"([1,2,3])");
	ASSERT_NE(p.doc, nullptr);
	EXPECT_EQ(select(p.doc, "$[0,0]"), "1|1");
	EXPECT_EQ(select(p.doc, "$[0,0,0]"), "1|1|1");
	EXPECT_EQ(select(p.doc, "$[*,*]"), "1|2|3|1|2|3");
	// And the order is the query's, not the document's.
	EXPECT_EQ(select(p.doc, "$[2,0]"), "3|1");
}

TEST(JsonPath, TheRootOnItsOwn) {
	Parsed p;
	p.doc = parse(R"({"a":1})");
	ASSERT_NE(p.doc, nullptr);
	EXPECT_EQ(select(p.doc, "$"), "{\"a\":1}");
	// Blank space is allowed between segments and inside brackets (2.1).
	EXPECT_EQ(select(p.doc, "$ .a"), "1");
	EXPECT_EQ(select(p.doc, "$[ 'a' ]"), "1");
	EXPECT_EQ(select(p.doc, "$ [ 'a' ]"), "1");
	/* But `segments = *(S segment)` puts the space *before* a segment, so a
	   query does not end with one - the compliance suite has `$ ` as an invalid
	   selector, and this test asserted the opposite until it was run against
	   the suite. */
	EXPECT_EQ(select(p.doc, "$ "), "!17");
	EXPECT_EQ(select(p.doc, "$.a "), "!17");
	EXPECT_EQ(select(p.doc, "$[ 'a' ] "), "!17");
}

// ===========================================================================
// What is refused
// ===========================================================================

TEST(JsonPath, MalformedQueriesAreRefused) {
	Parsed p;
	p.doc = parse(R"({"a":[1,2]})");
	ASSERT_NE(p.doc, nullptr);

	const char * refused[] = {
	    "",           // a query begins with $
	    "a",          //
	    ".a",         //
	    "$a",         // a segment begins with . or [
	    "$.",         // a name has to follow
	    "$..",        //
	    "$.[0]",      // 2.5.1.1: the shorthand is a name, not a bracket
	    "$[",         //
	    "$[]",        // at least one selector
	    "$[0",        //
	    "$[0,]",      // no trailing comma
	    "$['a]",      // unclosed name
	    "$[01]",      // no leading zero
	    "$[-0]",      // -0 is not an integer
	    "$[1.5]",     // not an integer
	    "$[a]",       // a bare word is not a selector
	    "$['a'b]",    // no separator
	    "$..*.",      //
	    "$['\\q']",   // unknown escape
	    "$['\\uD800']", // a lone surrogate
	};
	for (const char * query : refused) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Path * path =
		    gtext_json_path_compile(query, SIZE_MAX, nullptr, &err);
		EXPECT_EQ(path, nullptr) << "accepted [" << query << "]";
		if (path) {
			gtext_json_path_free(path);
			continue;
		}
		EXPECT_EQ(err.code, GTEXT_JSON_E_PATH) << "[" << query << "]";
		EXPECT_NE(err.message, nullptr) << "[" << query << "]";
	}
}

/* The filter selector is refused as unimplemented rather than ignored: a query
   whose filter was dropped would select every element of the array instead of
   the ones asked for, which is a wrong answer where this is a missing feature.
   The status says which of the two it is. */
TEST(JsonPath, AFilterIsRefusedAsUnsupported) {
	for (const char * query : {"$[?@.a]", "$.a[?@.b == 1]", "$..[?@]",
	         "$[?@.a][0]", "$[0][?@.a]"}) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Path * path =
		    gtext_json_path_compile(query, SIZE_MAX, nullptr, &err);
		EXPECT_EQ(path, nullptr) << "accepted [" << query << "]";
		EXPECT_EQ(err.code, GTEXT_JSON_E_PATH_UNSUPPORTED)
		    << "[" << query << "] gave " << (int)err.code;
		if (path) {
			gtext_json_path_free(path);
		}
	}
}

// ===========================================================================
// The API
// ===========================================================================

TEST(JsonPath, ACompiledQueryIsReusable) {
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Path * path =
	    gtext_json_path_compile("$..price", SIZE_MAX, nullptr, &err);
	ASSERT_NE(path, nullptr) << (err.message ? err.message : "");

	for (const char * text : {R"({"price":1})", R"({"a":{"price":2}})",
	         R"([{"price":3},{"price":4}])"}) {
		Parsed p;
		p.doc = parse(text);
		ASSERT_NE(p.doc, nullptr);
		GTEXT_JSON_Path_Result result;
		ASSERT_EQ(gtext_json_path_select(path, p.doc, &result), GTEXT_JSON_OK);
		EXPECT_GT(result.count, 0u) << text;
		gtext_json_path_result_free(&result);
	}
	gtext_json_path_free(path);
}

TEST(JsonPath, AnEmptyResultIsNotAnError) {
	Parsed p;
	p.doc = parse(R"({"a":1})");
	ASSERT_NE(p.doc, nullptr);
	GTEXT_JSON_Path_Result result;
	ASSERT_EQ(gtext_json_path_query(p.doc, "$.nothing", SIZE_MAX, nullptr,
	              &result, nullptr),
	    GTEXT_JSON_OK);
	EXPECT_EQ(result.count, 0u);
	EXPECT_EQ(result.nodes, nullptr);
	gtext_json_path_result_free(&result);
	// Freeing twice is safe.
	gtext_json_path_result_free(&result);
}

TEST(JsonPath, NullArgumentsAreRefusedRatherThanCrashing) {
	Parsed p;
	p.doc = parse(R"({"a":1})");
	ASSERT_NE(p.doc, nullptr);
	GTEXT_JSON_Path_Result result;
	EXPECT_EQ(gtext_json_path_query(nullptr, "$", SIZE_MAX, nullptr, &result,
	              nullptr),
	    GTEXT_JSON_E_INVALID);
	EXPECT_EQ(gtext_json_path_query(p.doc, nullptr, SIZE_MAX, nullptr, &result,
	              nullptr),
	    GTEXT_JSON_E_INVALID);
	EXPECT_EQ(gtext_json_path_select(nullptr, p.doc, &result),
	    GTEXT_JSON_E_INVALID);
	gtext_json_path_free(nullptr);
	gtext_json_path_result_free(nullptr);
}

/* The length may be given rather than measured, so a query need not be
   NUL-terminated and one that is longer than the length given is cut off. */
TEST(JsonPath, TheLengthIsRespected) {
	Parsed p;
	p.doc = parse(R"({"a":1,"ab":2})");
	ASSERT_NE(p.doc, nullptr);

	GTEXT_JSON_Path_Result result;
	ASSERT_EQ(gtext_json_path_query(p.doc, "$.ab", 3, nullptr, &result, nullptr),
	    GTEXT_JSON_OK);
	ASSERT_EQ(result.count, 1u);
	EXPECT_EQ(render(result.nodes[0]), "1");
	gtext_json_path_result_free(&result);
}

/* A query over a deeply nested document must not put that depth on the C
   stack: the descendant walk is the one place a caller's document decides how
   far this library recurses, which is why it uses an explicit stack.

   The document is built through the DOM API rather than parsed, because the
   *parser* is recursive: measured at about 450 bytes of stack per level, so a
   document this deep overflows it long before reaching the query. That is what
   max_depth is for, and the JSON page now says what it costs. Freeing is
   arena-based and not recursive either, so 100,000 levels go through all three
   steps here. */
TEST(JsonPath, ADeepDocumentDoesNotOverflowTheStack) {
	const size_t depth = 100000;
	GTEXT_JSON_Value * inner = gtext_json_new_number_i64(42);
	ASSERT_NE(inner, nullptr);
	for (size_t i = 0; i < depth; i++) {
		GTEXT_JSON_Value * obj = gtext_json_new_object();
		ASSERT_NE(obj, nullptr);
		ASSERT_EQ(gtext_json_object_put(obj, "a", 1, inner), GTEXT_JSON_OK);
		inner = obj;
	}

	GTEXT_JSON_Path_Result result;
	ASSERT_EQ(gtext_json_path_query(
	              inner, "$..a", SIZE_MAX, nullptr, &result, nullptr),
	    GTEXT_JSON_OK);
	EXPECT_EQ(result.count, depth);
	gtext_json_path_result_free(&result);
	gtext_json_free(inner);
}
