/**
 * An anchor on a block collection.
 *
 * An anchor applies to the node that follows it, and in block context
 * nothing says whether that node is the next scalar or a collection whose
 * first key or item it is until the parser has seen what comes next. A flow
 * collection has a "[" or "{" for the anchor to arrive with; a block one has
 * no such token, so the anchor stayed on whichever scalar the stream could
 * attach it to - the first entry of a sequence, or the first key of a
 * mapping.
 *
 * Nothing reported an error. The alias simply resolved to the wrong node:
 *
 *     bill-to: &id001      *id001 gave the string "given" rather than the
 *         given  : Chris   mapping (spec example 2.27, suite case UGM3).
 *         family : Dumars
 *     ship-to: *id001
 *
 *     x: &anc              *anc gave 1 rather than [1].
 *       - 1
 *     y: *anc
 *
 * The tag half of this was fixed before - see the deviations section of
 * documentation/formats/yaml.md - and adopt_own_line_tag() is what does it.
 * The anchor half was never done, and is adopt_own_line_anchor() now.
 *
 * Only an anchor written on an earlier line moves. "&a key: 1" anchors the
 * key, exactly as "!custom key: 1" tags it; the two spellings differ in
 * nothing but where the property was written, which is why the event carries
 * anchor_line beside tag_line.
 *
 * Moving the anchor string was only half of it. The first scalar registers
 * the anchor in the alias table before adopt_own_line_anchor() runs, and
 * that entry stayed behind pointing at the scalar, so every case above -
 * which puts its alias *after* the collection, where the collection has
 * re-registered itself - was right while an alias *inside* the collection
 * was still bound to the scalar:
 *
 *     &O          *O gave the string "k" rather than the mapping, so a
 *     k: v        document both oracles read as recursive came back finite.
 *     j: *O
 *
 * unregister_anchor() takes the registration back with the string. The alias
 * then defers to the end of the parse exactly as it always did for the flow
 * spelling "&O [1, *O]", which was never wrong because there the anchor
 * arrives with the "[" and no scalar ever claims it.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	/* The alias is what shows where the anchor landed. */
	{"x: &anc\n  - 1\ny: *anc\n", "{\"x\": [1], \"y\": [1]}"},
	{"x: &anc\n  a: 1\ny: *anc\n", "{\"x\": {\"a\": 1}, \"y\": {\"a\": 1}}"},
	{"x: &anc\n  - 1\n  - 2\ny: *anc\n", "{\"x\": [1, 2], \"y\": [1, 2]}"},

	/* Spec example 2.27's shape, which is what found this. */
	{"bill-to: &id001\n    given  : Chris\n    family : Dumars\nship-to: *id001\n",
	 "{\"bill-to\": {\"given\": \"Chris\", \"family\": \"Dumars\"}, "
	 "\"ship-to\": {\"given\": \"Chris\", \"family\": \"Dumars\"}}"},

	/* A flow collection always worked: the anchor arrives with the "[". */
	{"x: &anc [1]\ny: *anc\n", "{\"x\": [1], \"y\": [1]}"},
	{"x: &anc {a: 1}\ny: *anc\n", "{\"x\": {\"a\": 1}, \"y\": {\"a\": 1}}"},

	/* A scalar keeps its own anchor. */
	{"x: &anc 1\ny: *anc\n", "{\"x\": 1, \"y\": 1}"},
	{"x: &anc\ny: *anc\n", "{\"x\": null, \"y\": null}"},

	/* An anchor on the key's own line belongs to the key, not the mapping,
	   which is the same split the tag rule makes. */
	{"x:\n  &k a: 1\ny: *k\n", "{\"x\": {\"a\": 1}, \"y\": \"a\"}"},

	/* At the root there is no enclosing collection to adopt it. The scalar
	   case is the one that reaches the rule at stack depth zero, where there
	   is no enclosing level to read at all. */
	{"&anc\n- 1\n", "[1]"},
	{"&anc\nfoo\n", "\"foo\""},

	/* An explicit key may be a collection rather than a scalar, and the
	   anchor it carries is not in the same place in the node. */
	{"? &k [1]\n: 2\n", "{[1]: 2}"},
	{"? &k\n  [1]\n: 2\n", "{[1]: 2}"},

	/* A flow collection that already has an anchor of its own keeps it: the
	   one written inside it belongs to the entry, not to the collection. */
	{"x: &outer [\n  &inner\n  1 ]\ny: *outer\n",
	 "{\"x\": [1], \"y\": [1]}"},
};

} // namespace

TEST(YamlCollectionAnchor, ReachesTheCollectionNotItsFirstScalar) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* The anchor has to be on the collection node itself, not merely resolvable
   through it: gtext_yaml_node_anchor() is what a caller walking the DOM
   sees, and it was empty. */
TEST(YamlCollectionAnchor, TheCollectionNodeCarriesTheAnchor) {
	const char *input = "x: &anc\n  a: 1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *x = gtext_yaml_mapping_get(root, "x");
	ASSERT_NE(x, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(x), GTEXT_YAML_MAPPING);
	EXPECT_STREQ(gtext_yaml_node_anchor(x), "anc");
	/* And the key it used to sit on no longer claims it. */
	const GTEXT_YAML_Node *key = nullptr, *val = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(x, 0, &key, &val));
	EXPECT_EQ(gtext_yaml_node_anchor(key), nullptr);
	gtext_yaml_free(doc);
}

/* An alias inside the collection the anchor was written for names that
   collection, which makes the document recursive. Identity is the assertion
   and not a rendering: the whole point is that the alias and the enclosing
   node are the same node, and no finite text distinguishes "the mapping"
   from "a copy of the mapping". */
TEST(YamlCollectionAnchor, AnAliasInsideTheCollectionNamesTheCollection) {
	struct Inner {
		const char *input;
		/* Index of the entry holding the alias. */
		size_t at;
		/* Whether the alias is that entry's key rather than its value. */
		bool as_key;
	};
	const Inner kInner[] = {
		{"&O\nk: v\nj: *O\n", 1, false},
		{"&O\n- 1\n- *O\n", 1, false},
		/* The alias as a key. This one was refused outright before the fix,
		   and for a reason worth keeping in view: *O resolved to the string
		   "k", which is already this mapping's first key, so a document with
		   two distinct keys came back "Duplicate mapping key". The wrong
		   answer was not only a wrong value - it was an error. */
		{"&O\nk: v\n*O : x\n", 1, true},
		/* The flow spelling, which has always been right and has to stay
		   right: it is the control for the mechanism, not for the syntax. */
		{"&O [1, *O]\n", 1, false},
		{"&O {k: v, j: *O}\n", 1, false},
	};
	for (const Inner &c : kInner) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.input, strlen(c.input), nullptr, &err);
		ASSERT_NE(doc, nullptr)
			<< ::testing::PrintToString(std::string(c.input)) << ": "
			<< (err.message ? err.message : "?");
		const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
		ASSERT_NE(root, nullptr);
		EXPECT_STREQ(gtext_yaml_node_anchor(root), "O")
			<< ::testing::PrintToString(std::string(c.input));

		const GTEXT_YAML_Node *alias = nullptr;
		if (gtext_yaml_node_type(root) == GTEXT_YAML_MAPPING) {
			const GTEXT_YAML_Node *key = nullptr, *value = nullptr;
			ASSERT_TRUE(gtext_yaml_mapping_get_at(root, c.at, &key, &value));
			alias = c.as_key ? key : value;
		} else {
			alias = gtext_yaml_sequence_get(root, c.at);
		}
		ASSERT_NE(alias, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(alias), GTEXT_YAML_ALIAS)
			<< ::testing::PrintToString(std::string(c.input));
		EXPECT_EQ(gtext_yaml_alias_target(alias), root)
			<< ::testing::PrintToString(std::string(c.input))
			<< ": alias resolved to "
			<< (gtext_yaml_alias_target(alias)
				? gtext_yaml_node_as_string(gtext_yaml_alias_target(alias))
				: "(nothing)");
		gtext_yaml_free(doc);
	}
}

/* Taking the registration back must not take back one the scalar is
   genuinely entitled to. An anchor written on the scalar's own line is the
   scalar's, and an alias to it is a string. These two are what tell a fix
   from an over-correction: they are the same documents as above with the
   anchor moved onto the line the scalar is on. */
TEST(YamlCollectionAnchor, AnAnchorTheScalarOwnsStaysWithTheScalar) {
	struct Kept {
		const char *input;
		const char *expected;
	};
	const Kept kKept[] = {
		{"k: &O v\nj: *O\n", "{\"k\": \"v\", \"j\": \"v\"}"},
		{"&O k: v\nj: *O\n", "{\"k\": \"v\", \"j\": \"k\"}"},
		/* A redefinition still wins for the alias that follows it, and the
		   earlier binding still stands for the alias that precedes it. */
		{"a: &O 1\nb: *O\nc: &O 2\nd: *O\n",
		 "{\"a\": 1, \"b\": 1, \"c\": 2, \"d\": 2}"},
	};
	for (const Kept &c : kKept) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

/* What the writer fuzzer found, and the reason this is more than a wrong
   value. The anchor reaches the mapping on the way *out* whether or not the
   alias does, so before the fix one document said two things: the alias was
   a string on the way in and a mapping on the way back. With
   require_string_keys the first parse accepted it and the second refused it.

   Both answers are defensible; disagreeing is not. The test asserts the
   agreement rather than either answer, so it still holds if the schema ever
   decides a mapping key differently.

   dupkeys is permissive here on purpose. The alias used as a key resolved to
   this mapping's own first key before the fix, so under the default
   GTEXT_YAML_DUPKEY_ERROR the first parse refused it and the case never
   reached the writer at all - and a test that reads a refusal as agreement
   would then have passed against the very document that found this. That is
   what round_tripped counts: skipping a case is allowed, skipping every case
   is not. */
TEST(YamlCollectionAnchor, TheSecondParseAgreesWithTheFirst) {
	const char *kInputs[] = {
		"&O\nk: v\nj: *O\n",     /* the alias is a value */
		"&O\nk: v\n*O : x\n",    /* the alias is a key */
		"&O\n- 1\n- *O\n",
	};
	size_t round_tripped = 0;
	for (const char *input : kInputs) {
		GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
		popts.require_string_keys = true;
		popts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;

		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), &popts, &err);
		if (!doc) continue;  /* refused on the way in, so never written */
		++round_tripped;

		GTEXT_YAML_Sink sink;
		ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
		GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
		ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &wopts), GTEXT_YAML_OK)
			<< ::testing::PrintToString(std::string(input));
		const std::string written(gtext_yaml_sink_buffer_data(&sink),
			gtext_yaml_sink_buffer_size(&sink));
		gtext_yaml_sink_buffer_free(&sink);

		GTEXT_YAML_Error err2;
		memset(&err2, 0, sizeof(err2));
		GTEXT_YAML_Document *again =
			gtext_yaml_parse(written.c_str(), written.size(), &popts, &err2);
		EXPECT_NE(again, nullptr)
			<< ::testing::PrintToString(std::string(input))
			<< " was accepted, written as " << written << ", and then refused: "
			<< (err2.message ? err2.message : "?");
		if (again) gtext_yaml_free(again);
		gtext_yaml_free(doc);
	}
	/* A case refused on the way in cannot disagree with itself, so the loop
	   above is allowed to skip one. What it is not allowed to do is skip them
	   all and report that as agreement. */
	EXPECT_GT(round_tripped, 0u)
		<< "every input was refused before reaching the writer";
}

/* A recursive document is finite to walk, because an alias is a node in its
   own right and nothing expands it. These are the four places the note on
   this defect listed as reasons not to fix it; each is asked here so that a
   change to any of them is seen against this shape rather than against a
   finite one.

   Unlike the tests above, this one passed before the fix as well - it had to,
   because the document it names was not yet recursive and so the question was
   being asked of an ordinary mapping. It is a guard for what comes next, not
   a witness for what was wrong. */
TEST(YamlCollectionAnchor, ARecursiveDocumentIsStillFiniteToHandle) {
	const char *input = "&O\nk: v\nj: *O\n";

	/* max_alias_expansion counts the aliases written, not the expansions a
	   reader could take, so one alias is one alias however it points. */
	for (size_t cap = 1; cap <= 3; ++cap) {
		GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
		popts.max_alias_expansion = cap;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), &popts, &err);
		EXPECT_NE(doc, nullptr) << "cap " << cap << ": "
			<< (err.message ? err.message : "?");
		if (doc) gtext_yaml_free(doc);
	}

	/* Duplicate-key detection compares keys with nodes_equal(), which now has
	   a recursive value to walk into. */
	{
		const char *dup = "&O\na: *O\na: *O\n";
		GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
		popts.dupkeys = GTEXT_YAML_DUPKEY_ERROR;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(dup, strlen(dup), &popts, &err);
		EXPECT_EQ(doc, nullptr) << "a repeated key is a repeated key";
		if (doc) gtext_yaml_free(doc);
	}

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");

	/* The writer emits the alias by name, so it has nothing to recurse into
	   and the anchor it needs is on the node the alias names. */
	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	EXPECT_EQ(gtext_yaml_write_document(doc, &sink, &wopts), GTEXT_YAML_OK);
	const std::string written(gtext_yaml_sink_buffer_data(&sink),
		gtext_yaml_sink_buffer_size(&sink));
	gtext_yaml_sink_buffer_free(&sink);
	EXPECT_NE(written.find("&O"), std::string::npos) << written;
	EXPECT_NE(written.find("*O"), std::string::npos) << written;

	/* JSON has no way to say it, and the conversion says so rather than
	   running out of stack. */
	GTEXT_JSON_Value *json = nullptr;
	GTEXT_YAML_Error jerr;
	memset(&jerr, 0, sizeof(jerr));
	EXPECT_NE(gtext_yaml_to_json(doc, &json, &jerr), GTEXT_YAML_OK);
	EXPECT_EQ(json, nullptr);

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
