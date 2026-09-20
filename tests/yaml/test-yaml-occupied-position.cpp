/**
 * Block structure that a node has already taken.
 *
 * Four rules, all of the same shape: something is written where a node
 * already stands, and the parser used to find a reading for it instead of
 * refusing.
 *
 * A scalar at a block mapping's own indentation that no ":" ever claimed is
 * not a key. A trailing key with no value is ordinary - "a:" at the end of a
 * document is {"a": null}, and so is an explicit "? a" - but "top1:" over
 * "  key1: val1" over "top2" was giving {"top1": {...}, "top2": null},
 * inventing a pair out of a malformed line.
 *
 * A comment has to be preceded by white space unless it opens the line
 * (6.6). 'key: "value"# c' and "[a, b,#c" were being read as comments, which
 * quietly threw away the rest of the line. Both PyYAML and js-yaml accept
 * these; yaml-test-suite marks all three of its cases as errors, and the
 * grammar agrees.
 *
 * A block collection entry is preceded on its line only by indentation and
 * by the "-" or "?" of the entries containing it, because a compact entry
 * may follow one of those and nothing else. "key: - a" put a block sequence
 * on the line of the key that owns it, which s-l+block-collection's leading
 * s-l-comments forbids; "- { y: z }- invalid" started a second entry beside
 * a finished flow mapping.
 *
 * A key is the one exception, and only for properties: an anchor or a tag
 * belongs to the node after it, so "!!str true: 2" is a tagged key. That is
 * what separates it from "&anchor - x", where the anchor has no node to
 * attach to.
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

const char *const kRefused[] = {
	"foo:\n  bar\ninvalid\n",  /* a scalar at the mapping's own indent that no ":" claimed */
	"top1:\n  key1: val1\ntop2\n",  /* the same after a nested mapping */
	"key:\n - i1\n - i2\ninvalid\n",  /* and after a sequence */
	"key: value\nthis is #not a: key\n",  /* the "#" opens a comment, so the line has no ":" at all */
	"a: 1\nb\n",  /* the smallest version */
	"key: \"value\"# invalid\n",  /* a comment needs white space in front of it (6.6) */
	"[ a, b,#c\n]\n",  /* after a comma */
	"[ a, b ]#c\n",  /* after a closing bracket */
	"key: - a\n     - b\n",  /* a block sequence cannot open on its key's line */
	"&anchor - x\n",  /* nor can an anchor attach to a "-" */
	"- { y: z }- invalid\n",  /* nor can an entry start beside a finished flow mapping */
	"x: { y: z }in: valid\n",  /* nor can a key */
};

const Case kAccepted[] = {
	{"a: 1\nb:\n", "{\"a\": 1, \"b\": null}"},  /* a trailing key with a ":" and no value is null */
	{"a:\n", "{\"a\": null}"},  /* even on its own */
	{"{a}\n", "{\"a\": null}"},  /* a flow mapping key needs no ":" at all */
	{"{a: 1, b}\n", "{\"a\": 1, \"b\": null}"},  /* nor does the last of several */
	{"- - a\n", "[[\"a\"]]"},  /* a compact sequence may follow a "-" on its line */
	{"- - - a\n", "[[[\"a\"]]]"},  /* to any depth */
	{"a:\n- b\n", "{\"a\": [\"b\"]}"},  /* a sequence at its key's column, on the next line */
	{"- x: 1\n", "[{\"x\": 1}]"},  /* a compact mapping may follow a "-" too */
	{"!!str true: 2\n", "{\"true\": 2}"},  /* a tag belongs to the key it precedes */
	{"&x b: 2\n", "{\"b\": 2}"},  /* and so does an anchor */
	{"- !!str x: 1\n", "[{\"x\": 1}]"},  /* both allowances at once */
	{"a: 1 # ok\n", "{\"a\": 1}"},  /* a comment with white space in front of it */
	{"# c\na: 1\n", "{\"a\": 1}"},  /* or at the start of a line */
	{"a: b#notcomment\n", "{\"a\": \"b#notcomment\"}"},  /* a "#" inside a plain scalar is content */
	{"a: 1\n  # c\nb: 2\n", "{\"a\": 1, \"b\": 2}"},  /* an indented comment line */
	{"? a\n: 1\nb: 2\n", "{\"a\": 1, \"b\": 2}"},  /* an explicit key claims its key as a ":" does */
	{"- ? a\n  : b\n", "[{\"a\": \"b\"}]"},  /* even compactly after a "-" */
	{"? a\n: - b\n  - c\n", "{\"a\": [\"b\", \"c\"]}"},  /* and a compact sequence may follow an explicit key's ":" */
	{"? a\n: b: c\n", "{\"a\": {\"b\": \"c\"}}"},  /* as may a compact mapping */
	{"- - b: c\n", "[[{\"b\": \"c\"}]]"},  /* through two levels of "-" */
};

}  // namespace

TEST(YamlOccupiedPosition, Refused) {
	for (const char *input : kRefused) {
		EXPECT_EQ(Render(input), std::string(""))
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

TEST(YamlOccupiedPosition, StillAccepted) {
	for (const Case &c : kAccepted) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
