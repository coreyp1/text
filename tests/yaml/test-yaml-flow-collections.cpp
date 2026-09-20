/**
 * Flow collections: scalars that span lines, "[a: 1]" single-pair mappings,
 * and the separators between entries.
 *
 * Four faults are pinned here, three of which let malformed input through
 * rather than refusing it:
 *
 *  - A plain scalar in a flow collection did not fold across a line break,
 *    so "[a" over an indented "b]" was two entries rather than one scalar.
 *  - "[a: 1]" - a single-pair mapping written straight into a flow sequence -
 *    did not parse at all. The ":" fell through to the block-mapping path and
 *    pushed a block level inside the sequence, which swallowed the "]".
 *  - Input that ended inside a flow collection produced a document with a
 *    NULL root and reported success, so a caller checking only for a NULL
 *    document got an empty one instead of an error.
 *  - Two entries with no "," between them were accepted, so "[a" over "b]"
 *    quietly became a two-entry sequence.
 *
 * Expectations are js-yaml's, which implements YAML 1.2. PyYAML is the 1.1
 * oracle used elsewhere in this suite and differs on several of these: it
 * folds a flow scalar across a break with no indentation at all, and rejects
 * tabs that 1.2 allows. Where the two disagree this follows 1.2.
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
	{"key: [a\n  b]\n", "{\"key\": [\"a b\"]}"},
	{"key: [a\n  b, c]\n", "{\"key\": [\"a b\", \"c\"]}"},
	{"key: [a,\n  b]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: {x: a\n  b}\n", "{\"key\": {\"x\": \"a b\"}}"},
	{"key: [\n  a,\n  b\n]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: [a\n\n  b]\n", "{\"key\": [\"a\\nb\"]}"},
	{"key: [a b\n  c d]\n", "{\"key\": [\"a b c d\"]}"},
	{"key: [\n a\n]\n", "{\"key\": [\"a\"]}"},
	{"key: {\n  a: 1,\n  b: 2\n}\n", "{\"key\": {\"a\": 1, \"b\": 2}}"},
	{"key: [a - b\n , c]\n", "{\"key\": [\"a - b\", \"c\"]}"},
	{"key: [a,\nb]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: [a\nb]\n", nullptr},
	{"key: [a: 1]\n", "{\"key\": [{\"a\": 1}]}"},
	{"key: [a: 1, b: 2]\n", "{\"key\": [{\"a\": 1}, {\"b\": 2}]}"},
	{"key: [a: 1, b]\n", "{\"key\": [{\"a\": 1}, \"b\"]}"},
	{"key: [b, a: 1]\n", "{\"key\": [\"b\", {\"a\": 1}]}"},
	{"key: [a: 1, b: 2, c]\n", "{\"key\": [{\"a\": 1}, {\"b\": 2}, \"c\"]}"},
	{"key: [{a: 1}]\n", "{\"key\": [{\"a\": 1}]}"},
	{"key: [a: [1, 2]]\n", "{\"key\": [{\"a\": [1, 2]}]}"},
	{"key: [a: {b: 1}]\n", "{\"key\": [{\"a\": {\"b\": 1}}]}"},
	{"key: [a:]\n", "{\"key\": [{\"a\": null}]}"},
	{"key: [a: 1, a: 2]\n", "{\"key\": [{\"a\": 1}, {\"a\": 2}]}"},
	{"key: [[1]\n[2]]\n", nullptr},
	{"key: [{a: 1}\n{b: 2}]\n", nullptr},
	{"key: [[1] [2]]\n", nullptr},
	{"key: [a\n, b]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key: [[1], [2]]\n", "{\"key\": [[1], [2]]}"},
	{"key: [&x 1\n*x]\n", nullptr},
	{"key: [a]\n", "{\"key\": [\"a\"]}"},
	{"key: [[1]]\n", "{\"key\": [[1]]}"},
	{"key: [{a: 1}, {b: 2}]\n", "{\"key\": [{\"a\": 1}, {\"b\": 2}]}"},
	{"key: {a: 1\nb: 2}\n", nullptr},
	{"key: [1\n2]\n", nullptr},
	{"key: [\na,\nb\n]\n", "{\"key\": [\"a\", \"b\"]}"},
	{"key:\n- [\na,\nb\n]\n", "{\"key\": [[\"a\", \"b\"]]}"},
	{"key: {\na: 1,\nb: 2\n}\n", "{\"key\": {\"a\": 1, \"b\": 2}}"},
	{"- [\n1,\n2\n]\n", "[[1, 2]]"},
	{"key: [\na b,\nc\n]\n", "{\"key\": [\"a b\", \"c\"]}"},
};

} // namespace

TEST(YamlFlowCollections, MatchesReferenceImplementation) {
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

/* Input ending inside a flow collection is an error, not an empty document.
   Indentation says nothing about where "[" and "{" end, so the dedent rule
   deliberately leaves them alone and nothing else closed them either. */
TEST(YamlFlowCollections, RefusesInputEndingInsideAFlowCollection) {
	EXPECT_EQ(Render("key: [a, b\n"), std::string(""));
	EXPECT_EQ(Render("key: {a: 1\n"), std::string(""));
	EXPECT_EQ(Render("[unclosed\n"), std::string(""));
	/* The "#" opens a comment that runs past the "]". */
	EXPECT_EQ(Render("key: [a #b, c]\n"), std::string(""));
	/* gtext_yaml_parse_all() makes the same check. It had been left out, so
	   the multi-document entry point still accepted what the single-document
	   one refused - found by running yaml-test-suite, where every case goes
	   through parse_all. */
	{
		size_t count = 0;
		GTEXT_YAML_Error all_err;
		memset(&all_err, 0, sizeof(all_err));
		const char *bad = "{bad\n";
		GTEXT_YAML_Document **docs =
			gtext_yaml_parse_all(bad, strlen(bad), &count, nullptr, &all_err);
		EXPECT_EQ(docs, nullptr);
	}

	/* An empty document still has a NULL root, and that is not an error. */
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse("", 0, nullptr, &err);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_root(doc), nullptr);
	gtext_yaml_free(doc);
}

/* A collection may be the key of a single-pair mapping. js-yaml renders this
   as {"1": 2} only because a JavaScript object cannot have an array key; the
   DOM here keeps the sequence, as it does for the explicit "? [1]" spelling. */
TEST(YamlFlowCollections, ACollectionMayBeAPairsKey) {
	EXPECT_EQ(Render("key: [[1]: 2]\n"), std::string("{\"key\": [{[1]: 2}]}"));
	EXPECT_EQ(Render("? [1]\n: 2\n"), std::string("{[1]: 2}"));
}

/* Laying a flow collection out over several lines is ordinary, and the
   entries need no indentation of their own when a "," separates them. */
TEST(YamlFlowCollections, MultiLineLayoutIsUnaffected) {
	EXPECT_EQ(Render("key: [\na,\nb\n]\n"), std::string("{\"key\": [\"a\", \"b\"]}"));
	EXPECT_EQ(Render("key: {\na: 1,\nb: 2\n}\n"), std::string("{\"key\": {\"a\": 1, \"b\": 2}}"));
	EXPECT_EQ(Render("- [\n1,\n2\n]\n"), std::string("[[1, 2]]"));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
