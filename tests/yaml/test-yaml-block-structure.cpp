/**
 * Where a block collection ends, and where a key is allowed to sit.
 *
 * Two defects are pinned here. A block sequence may sit at the same column as
 * the key that owns it, and one written that way never closed: the parent's
 * next key was taken as another entry, so "a:" over "- 1" over "b: 2" gave
 * {"a": [1, {"b": 2}]} instead of {"a": [1], "b": 2}. And a key indented
 * deeper than its mapping was nested even with no key above waiting for a
 * value, which put a mapping where a key belongs: "a: 1" over an indented
 * "b: 2" gave {"a": 1, {"b": 2}: null} rather than being refused.
 *
 * Expectations are PyYAML's for the same input; the accepted ones are its
 * json.dumps output, which Render() below reproduces exactly.
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

/* The rendered document, or an empty string when it was refused. */
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
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	{"a:\n- 1\nb: 2\n", "{\"a\": [1], \"b\": 2}"},
	{"a:\n  - 1\nb: 2\n", "{\"a\": [1], \"b\": 2}"},
	{"a:\n- 1\n- 2\nb: 2\n", "{\"a\": [1, 2], \"b\": 2}"},
	{"a:\n  - 1\n  - 2\nb: 2\n", "{\"a\": [1, 2], \"b\": 2}"},
	{"a:\n- 1\nb: 2\nc: 3\n", "{\"a\": [1], \"b\": 2, \"c\": 3}"},
	{"x:\n  y:\n  - 1\n  z: 2\n", "{\"x\": {\"y\": [1], \"z\": 2}}"},
	{"x:\n  y:\n    - 1\n  z: 2\n", "{\"x\": {\"y\": [1], \"z\": 2}}"},
	{"a:\n- 1\n- 2\n", "{\"a\": [1, 2]}"},
	{"a:\n- - 1\n- 2\nb: 3\n", "{\"a\": [[1], 2], \"b\": 3}"},
	{"a:\n- x: 1\n  y: 2\nb: 3\n", "{\"a\": [{\"x\": 1, \"y\": 2}], \"b\": 3}"},
	{"a:\n- x: 1\nb: 3\n", "{\"a\": [{\"x\": 1}], \"b\": 3}"},
	{"a:\n  b: 1\n", "{\"a\": {\"b\": 1}}"},
	{"a: 1\nb: 2\n", "{\"a\": 1, \"b\": 2}"},
	{"a:\n  b: 1\n  c: 2\n", "{\"a\": {\"b\": 1, \"c\": 2}}"},
	{"a:\n  b: 1\nc: 2\n", "{\"a\": {\"b\": 1}, \"c\": 2}"},
	{"a: 1\n  b: 2\n", nullptr},
	{"a: 1\n b: 2\n", nullptr},
	{"a: 1\n\n  b: 2\n", nullptr},
	{"a: {x: 1}\n  b: 2\n", nullptr},
};

} // namespace

TEST(YamlBlockStructure, MatchesReferenceImplementation) {
	for (const Case &c : kCases) {
		const std::string got = Render(c.input);
		if (c.expected) {
			EXPECT_EQ(got, std::string(c.expected))
				<< "input: " << ::testing::PrintToString(std::string(c.input));
		} else {
			EXPECT_EQ(got, std::string(""))
				<< "should have been refused, input: "
				<< ::testing::PrintToString(std::string(c.input));
		}
	}
}

/* A scalar indented past its block mapping is that mapping's value, and only
   while a key above is still waiting for one. With every key already paired
   there is nothing for it to be, and it used to become a trailing key with a
   null value - so a block scalar followed by a line that dedents out of it,
   but not back to the mapping, produced {"a": "deep\n", "shallow": null} for
   input neither PyYAML nor js-yaml accepts. */
TEST(YamlBlockStructure, RefusesScalarsWithNoPlaceToGo) {
	EXPECT_EQ(Render("a: |\n    deep\n  shallow\n"), std::string(""));
	EXPECT_EQ(Render("a: |\n  one\n b\n"), std::string(""));
	EXPECT_EQ(Render("a: >\n    one\n  two\n"), std::string(""));
	/* A plain value does continue onto an indented line, so this one stands:
	   the rule above fires only where the value is already complete. */
	EXPECT_EQ(Render("a: 1\n  b\n"), std::string("{\"a\": \"1 b\"}"));
}

/* The column that decides it is the line's first non-space, not the scalar's
   own: a tag or anchor sits before the scalar and belongs to the same node,
   so "!!str true" as a key starts where the tag does. */
TEST(YamlBlockStructure, ATaggedKeyStartsWhereItsTagDoes) {
	EXPECT_EQ(Render("true: 1\n!!str true: 2\n"),
		std::string("{true: 1, \"true\": 2}"));
	EXPECT_EQ(Render("a: 1\n&x b: 2\n"), std::string("{\"a\": 1, \"b\": 2}"));
}

/* A key inside a block sequence entry whose scalar is already complete. */
TEST(YamlBlockStructure, RefusesAKeyInsideACompleteSequenceEntry) {
	EXPECT_EQ(Render("a:\n- 1\n  b: 2\n"), std::string(""));
}

/* The scalar before a ":" is its key, held provisionally as the previous
   key's value until the ":" claims it. With none outstanding the ":" has no
   key at all, and "key: a : b" used to yield {"key": "a", "b": null} - the
   tail of a value silently turned into a pair. A ":" with no space after it
   is ordinary content and is unaffected. */
TEST(YamlBlockStructure, RefusesAColonWithNoKeyBeforeIt) {
	EXPECT_EQ(Render("key: a : b\n"), std::string(""));
	EXPECT_EQ(Render("key: a: b\n"), std::string(""));
	EXPECT_EQ(Render("a: 1\n: 2\n"), std::string(""));
	EXPECT_EQ(Render("key: a :b\n"), std::string("{\"key\": \"a :b\"}"));
	EXPECT_EQ(Render("a: 1\nb: 2\n"), std::string("{\"a\": 1, \"b\": 2}"));
	EXPECT_EQ(Render("? a\n: 1\nb: 2\n"), std::string("{\"a\": 1, \"b\": 2}"));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
