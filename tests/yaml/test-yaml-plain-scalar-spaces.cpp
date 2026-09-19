/**
 * Plain scalars that hold white space: across lines in block context
 * (7.3.3 ns-plain-multi-line) and within a flow collection.
 *
 * Both were wrong in the same way and both corrupted valid documents rather
 * than refusing them. A flow plain scalar ended at its first space, so
 * "[a b, c]" came out as three entries instead of two and "{k: v w, j: x}"
 * was scrambled outright. A block plain scalar never continued onto the next
 * line, so "key: a" over an indented "b" dropped the continuation and turned
 * it into a key of its own - with "key: a", an indented "b", then "other: 1"
 * giving {"key": "a", "b": "other", 1: null}, an integer standing as a key.
 *
 * Expectations are PyYAML's json.dumps output for the same input, which
 * Render() reproduces exactly.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

namespace {

void RenderInto(const GTEXT_YAML_Node *n, std::string &out) {
	if (!n) { out += "<null-node>"; return; }
	switch (gtext_yaml_node_type(n)) {
	case GTEXT_YAML_NULL:
		out += "null";
		return;
	case GTEXT_YAML_BOOL:
	case GTEXT_YAML_INT:
	case GTEXT_YAML_FLOAT: {
		const char *s = gtext_yaml_node_as_string(n);
		out += s ? s : "<none>";
		return;
	}
	case GTEXT_YAML_STRING: {
		const char *s = gtext_yaml_node_as_string(n);
		out += '"';
		for (; s && *s; ++s) {
			if (*s == '\n') out += "\\n";
			else if (*s == '\t') out += "\\t";
			else if (*s == '"') out += "\\\"";
			else if (*s == '\\') out += "\\\\";
			else out += *s;
		}
		out += '"';
		return;
	}
	case GTEXT_YAML_SEQUENCE:
	case GTEXT_YAML_SET:
	case GTEXT_YAML_OMAP:
	case GTEXT_YAML_PAIRS: {
		out += '[';
		const size_t len = gtext_yaml_sequence_length(n);
		for (size_t i = 0; i < len; ++i) {
			if (i) out += ", ";
			RenderInto(gtext_yaml_sequence_get(n, i), out);
		}
		out += ']';
		return;
	}
	case GTEXT_YAML_MAPPING: {
		out += '{';
		const size_t len = gtext_yaml_mapping_size(n);
		for (size_t i = 0; i < len; ++i) {
			const GTEXT_YAML_Node *k = nullptr, *v = nullptr;
			gtext_yaml_mapping_get_at(n, i, &k, &v);
			if (i) out += ", ";
			RenderInto(k, out);
			out += ": ";
			RenderInto(v, out);
		}
		out += '}';
		return;
	}
	default:
		out += "<other>";
		return;
	}
}

std::string Render(const char *input) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	if (!doc) return "";
	std::string out;
	RenderInto(gtext_yaml_document_root(doc), out);
	gtext_yaml_free(doc);
	return out;
}

struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	{"key: [a - b, c]\n", "{\"key\": [\"a - b\", \"c\"]}"},
	{"key: [a-b, c]\n", "{\"key\": [\"a-b\", \"c\"]}"},
	{"key: [a b, c]\n", "{\"key\": [\"a b\", \"c\"]}"},
	{"key: {x: a b}\n", "{\"key\": {\"x\": \"a b\"}}"},
	{"key: [a, b]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: [ a , b ]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: [a b c, d e]\n", "{\"key\": [\"a b c\", \"d e\"]}"},
	{"key: {k: v w, j: x}\n", "{\"key\": {\"k\": \"v w\", \"j\": \"x\"}}"},
	{"key: [a - b]\n", "{\"key\": [\"a - b\"]}"},
	{"key: [\"a - b\", c]\n", "{\"key\": [\"a - b\", \"c\"]}"},
	{"key: [1 2, 3]\n", "{\"key\": [\"1 2\", 3]}"},
	{"key: [a  b]\n", "{\"key\": [\"a  b\"]}"},
	{"key: {a b: c d}\n", "{\"key\": {\"a b\": \"c d\"}}"},
	{"key: [[a b], c]\n", "{\"key\": [[\"a b\"], \"c\"]}"},
	{"key: [a#b, c]\n", "{\"key\": [\"a#b\", \"c\"]}"},
	{"key: a\n  b\n", "{\"key\": \"a b\"}"},
	{"key: a\n  b\n  c\n", "{\"key\": \"a b c\"}"},
	{"key: a\n  b\nother: 1\n", "{\"key\": \"a b\", \"other\": 1}"},
	{"key: a\n\n  b\n", "{\"key\": \"a\\nb\"}"},
	{"a:\n  b: x\n    y\n  c: 1\n", "{\"a\": {\"b\": \"x y\", \"c\": 1}}"},
	{"key: a\nother: 1\n", "{\"key\": \"a\", \"other\": 1}"},
	{"- a\n  b\n- c\n", "[\"a b\", \"c\"]"},
	{"key: a\n b\n", "{\"key\": \"a b\"}"},
	{"key:\n  a\n  b\n", "{\"key\": \"a b\"}"},
	{"key: 1\n  2\n", "{\"key\": \"1 2\"}"},
	{"key: a\n  - b\n", "{\"key\": \"a - b\"}"},
	{"{a, b}\n", "{\"a\": null, \"b\": null}"},
	{"{a}\n", "{\"a\": null}"},
	{"{a: 1, b}\n", "{\"a\": 1, \"b\": null}"},
	{"{a, b: 2}\n", "{\"a\": null, \"b\": 2}"},
	{"{a: 1, b: 2}\n", "{\"a\": 1, \"b\": 2}"},
	{"{a, b, c}\n", "{\"a\": null, \"b\": null, \"c\": null}"},
	{"[{a, b}, c]\n", "[{\"a\": null, \"b\": null}, \"c\"]"},
	{"{a: 1, b, c: 3}\n", "{\"a\": 1, \"b\": null, \"c\": 3}"},
	{"{a:{b:{c:1}}}\n", "{\"a\": {\"b\": {\"c:1\": null}}}"},
	{"{a:[1,{b:2}]}\n", "{\"a\": [1, {\"b:2\": null}]}"},
	{"{users:[{name:alice,age:30},{name:bob,age:25}],count:2}\n", "{\"users\": [{\"name:alice\": null, \"age:30\": null}, {\"name:bob\": null, \"age:25\": null}], \"count:2\": null}"},
	{"{a: {b: {c: 1}}}\n", "{\"a\": {\"b\": {\"c\": 1}}}"},
	{"{c:1}\n", "{\"c:1\": null}"},
	{"[[1,2],[3,4]]\n", "[[1, 2], [3, 4]]"},
	{"[[[[[hello]]]]]\n", "[[[[[\"hello\"]]]]]"},
	{"[[],[[]],{}]\n", "[[], [[]], {}]"},
	{"{\"a\":1}\n", "{\"a\": 1}"},
	{"{\"a\": 1}\n", "{\"a\": 1}"},
	{"? a\n: 1\nb: 2\n", "{\"a\": 1, \"b\": 2}"},
	{"? a\n? b\n", "{\"a\": null, \"b\": null}"},
	{"? a\n", "{\"a\": null}"},
	{"&anchor value\n*anchor\n", "\"value *anchor\""},
	{"a\n? b\n", "\"a ? b\""},
	{"a\n*x\n", "\"a *x\""},
};

} // namespace

TEST(YamlPlainScalarSpaces, MatchesReferenceImplementation) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* A continuation line cannot carry a key: the scalar above has already
   claimed those lines, and splitting it there would quietly rearrange the
   mapping. The parser refuses these through its existing rule that a key
   must sit on the same line as its ':'; a second check in the scanner was
   written and then removed, because no input reached it that the parser had
   not already caught. */
TEST(YamlPlainScalarSpaces, RefusesAKeyOnAContinuationLine) {
	EXPECT_EQ(Render("key: a\n  b: 1\n"), std::string(""));
	EXPECT_EQ(Render("key:\n  a\n  b: 1\n"), std::string(""));
	EXPECT_EQ(Render("- a\n  b: 1\n"), std::string(""));
	EXPECT_EQ(Render("a\n  b: 1\n"), std::string(""));
	EXPECT_EQ(Render("key: a\n  b\n  c: 1\n"), std::string(""));
}

/* A scalar never folds across a document marker or a comment line. */
TEST(YamlPlainScalarSpaces, StopsAtADocumentMarkerOrComment) {
	EXPECT_EQ(Render("scalar\n---\n- a\n"), std::string("\"scalar\""));
	EXPECT_EQ(Render("a\n#c\n"), std::string("\"a\""));
}

/* Still open, and each differs from PyYAML.

   A plain scalar in a flow collection does not fold across a line break, so
   "[a" over an indented "b]" stays two entries. A single-pair mapping
   written directly in a flow sequence ("[a: 1]") is not built. And two
   malformed inputs are accepted rather than refused: a tab inside a flow
   plain scalar, and a comment opened inside a flow collection. */
TEST(YamlPlainScalarSpaces, DISABLED_FlowGapsAgainstTheReference) {
	EXPECT_EQ(Render("key: [a\n  b]\n"), std::string("{\"key\": [\"a b\"]}"));
	EXPECT_EQ(Render("key: [a: 1]\n"), std::string("{\"key\": [{\"a\": 1}]}"));
	EXPECT_EQ(Render("key: [a\tb]\n"), std::string(""));
	EXPECT_EQ(Render("key: [a #b, c]\n"), std::string(""));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
