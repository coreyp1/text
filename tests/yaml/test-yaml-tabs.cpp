/**
 * Tabs in leading white space (6.1).
 *
 * Indentation is counted in spaces. A tab that follows it is ordinary
 * separation and is allowed; what a tab may not do is stand between the
 * indentation and a block collection entry, because l+block-mapping is
 * ( s-indent(n) ns-l-block-map-entry(n) )+ with nothing permitted in
 * between.
 *
 * Every tab in leading white space used to be refused outright, which took
 * six valid documents in yaml-test-suite with it: a line holding only a tab,
 * a tab in front of a flow collection at the root, and a tab between one
 * space of indentation and a plain value. Separately, a line of " \t" was
 * not recognised as empty when a plain scalar looked past it, so "foo: 1"
 * over that line gave foo the string "1 " instead of the number 1.
 *
 * Expectations are js-yaml's. PyYAML refuses every input here: 1.1 does not
 * allow a tab in leading white space at all, which is the rule this file
 * exists to stop applying.
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

const Case kValid[] = {
	{"foo:\n \tbar\n", "{\"foo\": \"bar\"}"},  /* one space of indentation, then a tab as separation */
	{" \t\nfoo: 1\n", "{\"foo\": 1}"},  /* a line of white space indents nothing */
	{"foo: 1\n\t\nbar: 2\n", "{\"foo\": 1, \"bar\": 2}"},  /* nor does a line of just a tab */
	{"foo: 1\n \t\nbar: 2\n", "{\"foo\": 1, \"bar\": 2}"},  /* or a space and a tab */
	{"\t[\n\t]\n", "[]"},  /* a flow collection at the root is reached across separation */
	{"\t{}\n", "{}"},  /* either kind */
	{"a: 1\n\t# comment\nb: 2\n", "{\"a\": 1, \"b\": 2}"},  /* a comment line indents nothing */
	{"- [\n\t\n foo\n ]\n", "[[\"foo\"]]"},  /* a blank line inside a flow collection */
	{"foo: \"bar\n \t \t baz \t \t \"\n", "{\"foo\": \"bar baz \\t \\t \"}"},  /* tabs inside a quoted scalar are content */
	{"\t[a: 1]\n", "[{\"a\": 1}]"},  /* the colons inside a flow collection are not block keys */
	{"\t{a: 1}\n", "{\"a\": 1}"},  /* nor in a flow mapping */
	{"foo:\n \t\"a: b\"\n", "{\"foo\": \"a: b\"}"},  /* nor one inside a quoted scalar */
	{"foo:\n \t'a: b'\n", "{\"foo\": \"a: b\"}"},  /* either kind of quote */
};

const char *const kInvalid[] = {
	"foo:\n  a: 1\n  \tb: 2\n",  /* a tab between the indentation and a block mapping key */
	"\tfoo: 1\n",  /* a key reached only through a tab */
	"\t- a\n",  /* and a sequence entry */
	"a:\n \t- 1\n",  /* an entry one space in and then a tab */
	"- [\n\tfoo,\n foo\n ]\n",  /* inside a flow collection the entry still needs s-indent(n) first */
};

}  // namespace

TEST(YamlTabs, SeparationIsAllowed) {
	for (const Case &c : kValid) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

TEST(YamlTabs, IndentationIsNot) {
	for (const char *input : kInvalid) {
		EXPECT_EQ(Render(input), std::string(""))
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
