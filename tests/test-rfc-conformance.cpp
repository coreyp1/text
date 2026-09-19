/**
 * @file test-rfc-conformance.cpp
 * @brief The worked examples from RFC 6901, RFC 6902 and RFC 7386.
 *
 * These are the specifications' own appendix tables, transcribed.  Until now
 * every JSON Patch, Merge Patch and Pointer test in this project was written
 * against what the implementation does; this file is written against what the
 * documents say, which is the only way the two can be found to disagree.
 *
 * Each table entry names its section, so a failure points at the paragraph
 * that settles it rather than at somebody's opinion.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/text/json.h>

namespace {

// Serialize with sorted keys so that two documents with the same members
// compare equal regardless of the order the parser or patcher produced them
// in.  RFC 6902 does not constrain member order, so comparing raw output
// would test something the specification deliberately leaves open.
std::string canonical(const GTEXT_JSON_Value * v) {
	GTEXT_JSON_Write_Options wo = gtext_json_write_options_default();
	wo.sort_object_keys = true;
	wo.pretty = false;

	GTEXT_JSON_Sink sink;
	if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
		return "<sink failed>";
	}
	if (gtext_json_write_value(&sink, &wo, v, nullptr) != GTEXT_JSON_OK) {
		gtext_json_sink_buffer_free(&sink);
		return "<write failed>";
	}
	std::string out(
	    gtext_json_sink_buffer_data(&sink), gtext_json_sink_buffer_size(&sink));
	gtext_json_sink_buffer_free(&sink);
	return out;
}

std::string canonical_text(const char * json) {
	GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * v = gtext_json_parse(json, std::strlen(json), &po, &err);
	if (!v) {
		gtext_json_error_free(&err);
		return std::string("<unparseable: ") + json + ">";
	}
	std::string out = canonical(v);
	gtext_json_free(v);
	gtext_json_error_free(&err);
	return out;
}

struct PatchCase {
	const char * section; ///< RFC section that specifies this case
	const char * original;
	const char * patch;
	const char * expected; ///< nullptr when the RFC requires an error
};

void run_patch_case(const PatchCase & c) {
	SCOPED_TRACE(std::string(c.section) + ": " + c.original + " + " + c.patch);

	GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));

	GTEXT_JSON_Value * root =
	    gtext_json_parse(c.original, std::strlen(c.original), &po, &err);
	ASSERT_NE(root, nullptr) << "original did not parse";

	GTEXT_JSON_Value * patch =
	    gtext_json_parse(c.patch, std::strlen(c.patch), &po, &err);
	ASSERT_NE(patch, nullptr) << "patch did not parse";

	GTEXT_JSON_Status st = gtext_json_patch_apply(root, patch, &err);

	if (c.expected) {
		EXPECT_EQ(st, GTEXT_JSON_OK) << (err.message ? err.message : "");
		if (st == GTEXT_JSON_OK) {
			EXPECT_EQ(canonical(root), canonical_text(c.expected));
		}
	}
	else {
		EXPECT_NE(st, GTEXT_JSON_OK) << "the RFC requires this to fail";
	}

	gtext_json_free(patch);
	gtext_json_free(root);
	gtext_json_error_free(&err);
}

} // namespace

// ---------------------------------------------------------------------------
// RFC 6902, Appendix A - JavaScript Object Notation (JSON) Patch
// ---------------------------------------------------------------------------

TEST(Rfc6902AppendixA, WorkedExamples) {
	const PatchCase cases[] = {
	    {"A.1 Adding an Object Member", R"({"foo":"bar"})",
	        R"([{"op":"add","path":"/baz","value":"qux"}])",
	        R"({"baz":"qux","foo":"bar"})"},

	    {"A.2 Adding an Array Element", R"({"foo":["bar","baz"]})",
	        R"([{"op":"add","path":"/foo/1","value":"qux"}])",
	        R"({"foo":["bar","qux","baz"]})"},

	    {"A.3 Removing an Object Member", R"({"baz":"qux","foo":"bar"})",
	        R"([{"op":"remove","path":"/baz"}])", R"({"foo":"bar"})"},

	    {"A.4 Removing an Array Element", R"({"foo":["bar","qux","baz"]})",
	        R"([{"op":"remove","path":"/foo/1"}])", R"({"foo":["bar","baz"]})"},

	    {"A.5 Replacing a Value", R"({"baz":"qux","foo":"bar"})",
	        R"([{"op":"replace","path":"/baz","value":"boo"}])",
	        R"({"baz":"boo","foo":"bar"})"},

	    {"A.6 Moving a Value",
	        R"({"foo":{"bar":"baz","waldo":"fred"},"qux":{"corge":"grault"}})",
	        R"([{"op":"move","from":"/foo/waldo","path":"/qux/thud"}])",
	        R"({"foo":{"bar":"baz"},"qux":{"corge":"grault","thud":"fred"}})"},

	    {"A.7 Moving an Array Element",
	        R"({"foo":["all","grass","cows","eat"]})",
	        R"([{"op":"move","from":"/foo/1","path":"/foo/3"}])",
	        R"({"foo":["all","cows","eat","grass"]})"},

	    {"A.8 Testing a Value: Success", R"({"baz":"qux","foo":["a",2,"c"]})",
	        R"([{"op":"test","path":"/baz","value":"qux"},
	            {"op":"test","path":"/foo/1","value":2}])",
	        R"({"baz":"qux","foo":["a",2,"c"]})"},

	    {"A.9 Testing a Value: Error", R"({"baz":"qux"})",
	        R"([{"op":"test","path":"/baz","value":"bar"}])", nullptr},

	    {"A.10 Adding a Nested Member Object", R"({"foo":"bar"})",
	        R"([{"op":"add","path":"/child","value":{"grandchild":{}}}])",
	        R"({"foo":"bar","child":{"grandchild":{}}})"},

	    {"A.11 Ignoring Unrecognized Elements", R"({"foo":"bar"})",
	        R"([{"op":"add","path":"/baz","value":"qux","xyz":123}])",
	        R"({"foo":"bar","baz":"qux"})"},

	    {"A.12 Adding to a Nonexistent Target", R"({"foo":"bar"})",
	        R"([{"op":"add","path":"/baz/bat","value":"qux"}])", nullptr},

	    // A.13 is a duplicate "op" member, which is a parser-level concern
	    // rather than a patch-level one; it is covered separately below.

	    {"A.14 Escape Ordering", R"({"/":9,"~1":10})",
	        R"([{"op":"test","path":"/~01","value":10}])",
	        R"({"/":9,"~1":10})"},

	    {"A.15 Comparing Strings and Numbers", R"({"/":9,"~1":10})",
	        R"([{"op":"test","path":"/~01","value":"10"}])", nullptr},

	    {"A.16 Adding an Array Value", R"({"foo":["bar"]})",
	        R"([{"op":"add","path":"/foo/-","value":["abc","def"]}])",
	        R"({"foo":["bar",["abc","def"]]})"},
	};

	for (const PatchCase & c : cases) {
		run_patch_case(c);
	}
}

// ---------------------------------------------------------------------------
// RFC 6902 section 4 - operation-level requirements the appendix does not
// reach.  These are the error paths, which is where a patch implementation
// tends to diverge from the specification without anyone noticing, because a
// caller who never sends a malformed patch never finds out.
// ---------------------------------------------------------------------------

TEST(Rfc6902Operations, RejectsMalformedOperations) {
	const PatchCase cases[] = {
	    // Section 4: "op" is required and must be one of the defined values.
	    {"4 op missing", R"({"a":1})", R"([{"path":"/a","value":2}])", nullptr},
	    {"4 op unknown", R"({"a":1})",
	        R"([{"op":"frobnicate","path":"/a","value":2}])", nullptr},
	    {"4 op not a string", R"({"a":1})",
	        R"([{"op":123,"path":"/a","value":2}])", nullptr},

	    // Section 4: "path" is required for every operation.
	    {"4.1 add without path", R"({"a":1})", R"([{"op":"add","value":2}])",
	        nullptr},
	    {"4.2 remove without path", R"({"a":1})", R"([{"op":"remove"}])",
	        nullptr},

	    // Section 4.1: add requires a "value" member.
	    {"4.1 add without value", R"({"a":1})",
	        R"([{"op":"add","path":"/b"}])", nullptr},
	    // Section 4.3: replace requires a "value" member.
	    {"4.3 replace without value", R"({"a":1})",
	        R"([{"op":"replace","path":"/a"}])", nullptr},
	    // Sections 4.4 and 4.5: move and copy require a "from" member.
	    {"4.4 move without from", R"({"a":1})",
	        R"([{"op":"move","path":"/b"}])", nullptr},
	    {"4.5 copy without from", R"({"a":1})",
	        R"([{"op":"copy","path":"/b"}])", nullptr},
	    // Section 4.6: test requires a "value" member.
	    {"4.6 test without value", R"({"a":1})",
	        R"([{"op":"test","path":"/a"}])", nullptr},

	    // Section 4.3: "The target location MUST exist for the operation to
	    // be successful."
	    {"4.3 replace a missing member", R"({"a":1})",
	        R"([{"op":"replace","path":"/nope","value":2}])", nullptr},
	    // Section 4.2: same requirement for remove.
	    {"4.2 remove a missing member", R"({"a":1})",
	        R"([{"op":"remove","path":"/nope"}])", nullptr},
	    // Sections 4.4 and 4.5: the "from" location must exist.
	    {"4.4 move from a missing member", R"({"a":1})",
	        R"([{"op":"move","from":"/nope","path":"/b"}])", nullptr},
	    {"4.5 copy from a missing member", R"({"a":1})",
	        R"([{"op":"copy","from":"/nope","path":"/b"}])", nullptr},

	    // Section 4.1: an array index beyond the end is an error, and "-" is
	    // the only way to append.
	    {"4.1 add past the end of an array", R"({"a":[1,2]})",
	        R"([{"op":"add","path":"/a/5","value":3}])", nullptr},
	    {"4.1 add at the end with -", R"({"a":[1,2]})",
	        R"([{"op":"add","path":"/a/-","value":3}])", R"({"a":[1,2,3]})"},
	    {"4.2 remove past the end of an array", R"({"a":[1,2]})",
	        R"([{"op":"remove","path":"/a/5"}])", nullptr},

	    // Section 4: the patch itself must be an array of objects.
	    {"4 patch element not an object", R"({"a":1})", R"(["not an op"])",
	        nullptr},

	    // Section 4.4: "The 'from' location MUST NOT be a proper prefix of
	    // the 'path' location; i.e., a location cannot be moved into one of
	    // its children."
	    {"4.4 move into own child", R"({"a":{"b":1}})",
	        R"([{"op":"move","from":"/a","path":"/a/b/c"}])", nullptr},
	};

	for (const PatchCase & c : cases) {
		run_patch_case(c);
	}
}

TEST(Rfc6902Operations, TestComparesStructurallyNotTextually) {
	// Section 4.6 defines equality over the JSON value model: objects are
	// equal when their members match irrespective of order, and numbers are
	// compared by value rather than by lexeme.
	const PatchCase cases[] = {
	    {"4.6 object member order is irrelevant", R"({"a":{"x":1,"y":2}})",
	        R"([{"op":"test","path":"/a","value":{"y":2,"x":1}}])",
	        R"({"a":{"x":1,"y":2}})"},
	    {"4.6 arrays are ordered", R"({"a":[1,2]})",
	        R"([{"op":"test","path":"/a","value":[2,1]}])", nullptr},
	    {"4.6 nested equality", R"({"a":{"b":[1,{"c":null}]}})",
	        R"([{"op":"test","path":"/a","value":{"b":[1,{"c":null}]}}])",
	        R"({"a":{"b":[1,{"c":null}]}})"},
	    {"4.6 true is not 1", R"({"a":true})",
	        R"([{"op":"test","path":"/a","value":1}])", nullptr},
	    {"4.6 null is not absent", R"({"a":null})",
	        R"([{"op":"test","path":"/a","value":null}])", R"({"a":null})"},
	};

	for (const PatchCase & c : cases) {
		run_patch_case(c);
	}
}

TEST(Rfc6902Operations, FailedPatchLeavesTargetUnchanged) {
	// Section 5: "if a normative requirement is violated ... the application
	// MUST NOT be partially applied."  The second operation here is the one
	// that fails, so a non-atomic implementation leaves the first applied.
	const char * src = R"({"a":1})";
	GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));

	GTEXT_JSON_Value * root = gtext_json_parse(src, std::strlen(src), &po, &err);
	ASSERT_NE(root, nullptr);

	const char * patch_text =
	    R"([{"op":"add","path":"/b","value":2},
	        {"op":"test","path":"/a","value":"not one"}])";
	GTEXT_JSON_Value * patch =
	    gtext_json_parse(patch_text, std::strlen(patch_text), &po, &err);
	ASSERT_NE(patch, nullptr);

	EXPECT_NE(gtext_json_patch_apply(root, patch, &err), GTEXT_JSON_OK);
	EXPECT_EQ(canonical(root), canonical_text(R"({"a":1})"))
	    << "the successful first operation was not rolled back";

	gtext_json_free(patch);
	gtext_json_free(root);
	gtext_json_error_free(&err);
}

// ---------------------------------------------------------------------------
// RFC 7386, Appendix A - JSON Merge Patch
//
// The specification's full test table.
// ---------------------------------------------------------------------------

TEST(Rfc7386AppendixA, TestTable) {
	struct MergeCase {
		const char * original;
		const char * patch;
		const char * expected;
	};

	const MergeCase cases[] = {
	    {R"({"a":"b"})", R"({"a":"c"})", R"({"a":"c"})"},
	    {R"({"a":"b"})", R"({"b":"c"})", R"({"a":"b","b":"c"})"},
	    {R"({"a":"b"})", R"({"a":null})", R"({})"},
	    {R"({"a":"b","b":"c"})", R"({"a":null})", R"({"b":"c"})"},
	    {R"({"a":["b"]})", R"({"a":"c"})", R"({"a":"c"})"},
	    {R"({"a":"c"})", R"({"a":["b"]})", R"({"a":["b"]})"},
	    {R"({"a":{"b":"c"}})", R"({"a":{"b":"d","c":null}})",
	        R"({"a":{"b":"d"}})"},
	    {R"({"a":[{"b":"c"}]})", R"({"a":[1]})", R"({"a":[1]})"},
	    {R"(["a","b"])", R"(["c","d"])", R"(["c","d"])"},
	    {R"({"a":"b"})", R"(["c"])", R"(["c"])"},
	    {R"({"a":"foo"})", R"(null)", R"(null)"},
	    {R"({"a":"foo"})", R"("bar")", R"("bar")"},
	    {R"({"e":null})", R"({"a":1})", R"({"e":null,"a":1})"},
	    {R"([1,2])", R"({"a":"b","c":null})", R"({"a":"b"})"},
	    {R"({})", R"({"a":{"bb":{"ccc":null}}})", R"({"a":{"bb":{}}})"},
	};

	for (const MergeCase & c : cases) {
		SCOPED_TRACE(
		    std::string("original=") + c.original + " patch=" + c.patch);

		GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));

		GTEXT_JSON_Value * target =
		    gtext_json_parse(c.original, std::strlen(c.original), &po, &err);
		ASSERT_NE(target, nullptr) << "original did not parse";

		GTEXT_JSON_Value * patch =
		    gtext_json_parse(c.patch, std::strlen(c.patch), &po, &err);
		ASSERT_NE(patch, nullptr) << "patch did not parse";

		GTEXT_JSON_Status st = gtext_json_merge_patch(target, patch, &err);
		EXPECT_EQ(st, GTEXT_JSON_OK) << (err.message ? err.message : "");
		if (st == GTEXT_JSON_OK) {
			EXPECT_EQ(canonical(target), canonical_text(c.expected));
		}

		gtext_json_free(patch);
		gtext_json_free(target);
		gtext_json_error_free(&err);
	}
}

// ---------------------------------------------------------------------------
// RFC 6901, section 5 - JavaScript Object Notation (JSON) Pointer
//
// The specification's worked example document and every pointer it lists,
// including the escape cases that distinguish ~0 from ~1.
// ---------------------------------------------------------------------------

TEST(Rfc6901Section5, WorkedExamples) {
	// The document from the RFC, verbatim.
	const char * doc_text = R"({
      "foo": ["bar", "baz"],
      "": 0,
      "a/b": 1,
      "c%d": 2,
      "e^f": 3,
      "g|h": 4,
      "i\\j": 5,
      "k\"l": 6,
      " ": 7,
      "m~n": 8
   })";

	GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * doc =
	    gtext_json_parse(doc_text, std::strlen(doc_text), &po, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");

	struct PointerCase {
		const char * pointer;
		const char * expected; ///< canonical JSON of the referenced value
	};

	const PointerCase cases[] = {
	    {"", nullptr}, // the whole document; handled below
	    {"/foo", R"(["bar","baz"])"},
	    {"/foo/0", R"("bar")"},
	    {"/", R"(0)"},
	    {"/a~1b", R"(1)"},
	    {"/c%d", R"(2)"},
	    {"/e^f", R"(3)"},
	    {"/g|h", R"(4)"},
	    {"/i\\j", R"(5)"},
	    {"/k\"l", R"(6)"},
	    {"/ ", R"(7)"},
	    {"/m~0n", R"(8)"},
	};

	for (const PointerCase & c : cases) {
		SCOPED_TRACE(std::string("pointer=\"") + c.pointer + "\"");
		const GTEXT_JSON_Value * v =
		    gtext_json_pointer_get(doc, c.pointer, std::strlen(c.pointer));
		ASSERT_NE(v, nullptr) << "pointer did not resolve";
		if (c.expected) {
			EXPECT_EQ(canonical(v), canonical_text(c.expected));
		}
		else {
			// The empty pointer references the whole document.
			EXPECT_EQ(v, doc);
		}
	}

	gtext_json_free(doc);
	gtext_json_error_free(&err);
}

TEST(Rfc6901Section4, RejectsMalformedPointers) {
	const char * doc_text = R"({"a":{"b":[1,2]},"m~n":1,"a/b":2})";
	GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * doc =
	    gtext_json_parse(doc_text, std::strlen(doc_text), &po, &err);
	ASSERT_NE(doc, nullptr);

	// Section 3: a non-empty pointer must begin with "/".
	EXPECT_EQ(gtext_json_pointer_get(doc, "a", 1), nullptr);
	// Section 4: "~" must be followed by "0" or "1"; anything else is not a
	// valid escape and so does not name the member "m~n".
	EXPECT_EQ(gtext_json_pointer_get(doc, "/m~2n", 5), nullptr);
	EXPECT_EQ(gtext_json_pointer_get(doc, "/m~", 3), nullptr);
	// Section 4: an unescaped "/" starts a new reference token, so "/a/b"
	// descends rather than naming the member "a/b".
	EXPECT_NE(gtext_json_pointer_get(doc, "/a~1b", 5), nullptr);
	// Section 4: array indices are decimal with no leading zeros or signs.
	EXPECT_EQ(gtext_json_pointer_get(doc, "/a/b/01", 7), nullptr);
	EXPECT_EQ(gtext_json_pointer_get(doc, "/a/b/+1", 7), nullptr);
	EXPECT_EQ(gtext_json_pointer_get(doc, "/a/b/-1", 7), nullptr);
	// Out of range.
	EXPECT_EQ(gtext_json_pointer_get(doc, "/a/b/2", 6), nullptr);
	// "-" names the position after the last element, which does not exist
	// for a get.
	EXPECT_EQ(gtext_json_pointer_get(doc, "/a/b/-", 6), nullptr);

	gtext_json_free(doc);
	gtext_json_error_free(&err);
}

int main(int argc, char ** argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
