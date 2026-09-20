/**
 * ":", "-" and "?" where nothing plain-safe follows them (5.3, 7.3.3, 7.4.2).
 *
 * All three are indicators only where white space, the end of the line, or -
 * inside a flow collection - a flow indicator follows. Everywhere else they
 * are ordinary plain characters.
 *
 * That was already true of a colon reached part way through a scalar, which
 * is why "key: a :b" gave "a :b". A colon that *began* a node was still
 * taken as an indicator, so "- ::vector" and "::" were refused for having no
 * key in front of the colon when both are ordinary plain scalars, and
 * yaml-test-suite's spec example 7.10 could not be parsed at all. In flow
 * context the scalar came back empty instead, and the collection was then
 * reported as never closed.
 *
 * Flow adds one more rule, the adjacent value: after a JSON-like node - a
 * quoted scalar, or a closing "]" or "}" - the colon may follow with nothing
 * between them. That is what makes '{"a":1}' a mapping rather than the
 * single scalar '"a":1'.
 *
 * "-" and "?" had the same gap, and there it cost data rather than a
 * refusal: "- !!int -2" gave [1, [2], 33], with -2 read as a nested
 * sequence holding 2, and "{?foo: bar}" lost its key entirely.
 *
 * Expectations are js-yaml's, which agree with PyYAML wherever PyYAML will
 * parse the input at all; 1.1 refuses a plain scalar that starts with a
 * colon. The one case where js-yaml is the odd one out is the adjacent value
 * on its own line, which it refuses and suite case 5MUD says is valid.
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

const Case kBlockCases[] = {
	{"- ::vector\n", "[\"::vector\"]"},  /* a colon that begins a node is content when a plain character follows */
	{"- :,\n", "[\":,\"]"},  /* a comma is plain-safe in block context, so this is one scalar */
	{"::\n", "{\":\": null}"},  /* the first colon is content, the second ends the key */
	{"a: ::b\n", "{\"a\": \"::b\"}"},  /* and mid-value likewise */
	{"::a: 1\n", "{\"::a\": 1}"},  /* a key may begin with one */
	{"- :a\n", "[\":a\"]"},  /* one colon is enough */
	{"x:\n  ::y\n", "{\"x\": \"::y\"}"},  /* on a continuation line too */
	{"key: a :b\n", "{\"key\": \"a :b\"}"},  /* unchanged: mid-scalar this already worked */
	{"a : b\n", "{\"a\": \"b\"}"},  /* unchanged: a colon with space either side ends the key */
};

const Case kDashAndQuestionCases[] = {
	{"- !!int -2\n", "[-2]"},  /* a negative number after a tag, not a nested sequence */
	{"safe dash: -foo\n", "{\"safe dash\": \"-foo\"}"},  /* a plain scalar may begin with a dash */
	{"safe question mark: ?foo\n", "{\"safe question mark\": \"?foo\"}"},  /* or a question mark */
	{"a: -1\n", "{\"a\": -1}"},  /* the ordinary negative number */
	{"- -1\n", "[-1]"},  /* as a sequence entry */
	{"[-1, 2]\n", "[-1, 2]"},  /* and inside a flow sequence */
	{"[a, -1]\n", "[\"a\", -1]"},  /* after a comma */
	{"{?foo: bar}\n", "{\"?foo\": \"bar\"}"},  /* a flow key beginning with a question mark */
	{"[?a]\n", "[\"?a\"]"},  /* and a flow entry */
	{"- ?foo\n", "[\"?foo\"]"},  /* in block context too */
	{"- a\n- b\n", "[\"a\", \"b\"]"},  /* unchanged: a dash with a space after it is an entry */
	{"- - a\n", "[[\"a\"]]"},  /* nested, likewise */
	{"? a\n: 1\n", "{\"a\": 1}"},  /* and a question mark with a space opens an explicit key */
	{"{? foo: bar}\n", "{\"foo\": \"bar\"}"},  /* including in flow */
};

const Case kFlowCases[] = {
	{"{x: :x}\n", "{\"x\": \":x\"}"},  /* a value may begin with a colon */
	{"[:x]\n", "[\":x\"]"},  /* so may a sequence entry */
	{"[a, :x]\n", "[\"a\", \":x\"]"},  /* after a comma as well */
	{"{:x: 1}\n", "{\":x\": 1}"},  /* and a key */
	{"[a:b, c]\n", "[\"a:b\", \"c\"]"},  /* unchanged: a colon between plain characters is content */
	{"{a:1}\n", "{\"a:1\": null}"},  /* unchanged: so the whole thing is one key */
	{"{a:{b:1}}\n", "{\"a\": {\"b:1\": null}}"},  /* but a flow indicator after the colon ends the key */
	{"{a:[1,{b:2}]}\n", "{\"a\": [1, {\"b:2\": null}]}"},  /* for every flow indicator */
	{"- { \"key\":value }\n", "[{\"key\": \"value\"}]"},  /* after a quoted key the colon may be adjacent (7.4.2) */
	{"- { \"key\"::value }\n", "[{\"key\": \":value\"}]"},  /* the first colon is that adjacency, the second is content */
	{"{\"adjacent\":value, \"empty\":}\n", "{\"adjacent\": \"value\", \"empty\": null}"},  /* spec example 7.18 */
};

}  // namespace

TEST(YamlColonContent, BlockContext) {
	for (const Case &c : kBlockCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

TEST(YamlColonContent, DashAndQuestionMark) {
	for (const Case &c : kDashAndQuestionCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

TEST(YamlColonContent, FlowContext) {
	for (const Case &c : kFlowCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* The adjacency survives a line break and a comment, because what matters is
   the last node rather than the last byte (suite cases 5MUD and K3WX). */
TEST(YamlColonContent, AnAdjacentValueMayBeOnTheNextLine) {
	EXPECT_EQ(Render("{ \"foo\"\n  :bar }\n"), std::string("{\"foo\": \"bar\"}"));
	EXPECT_EQ(Render("{ \"foo\" # comment\n  :bar }\n"),
		std::string("{\"foo\": \"bar\"}"));
}

/* And a colon that really has no key in front of it is still refused. */
TEST(YamlColonContent, AColonThatEndsNothingIsStillRefused) {
	EXPECT_EQ(Render("- :\n"), std::string(""));
	EXPECT_EQ(Render(":\n"), std::string(""));
	EXPECT_EQ(Render("a: 1\n: 2\n"), std::string(""));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
