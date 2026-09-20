/**
 * Two anchors, or two tags, with nothing written between them.
 *
 * A node carries at most one anchor and at most one tag (c-ns-properties,
 * 7.1). Two of a kind in a row therefore name two different nodes, and the
 * only thing that can stand between them without being written down is a
 * block collection starting where the second one is:
 *
 *     top1: &node1        &node1 is the mapping's,
 *       &k1 key1: val1    &k1 the key's
 *
 * Whether that collection opens is not known until the token after the node,
 * so the stream cannot place the first property when the second arrives. It
 * had one slot for each kind and simply overwrote it, which lost the outer
 * property in every case above - "ref: *node1" was then an unknown anchor,
 * and a valid document was refused - and accepted the case where no
 * collection opens at all and the two really did name one node:
 *
 *     top2: &node2        both name val2, which is an error
 *       &v2 val2
 *
 * Suite case 4JVG, the last one this library failed.
 *
 * The stream now sets the displaced property aside and reports both, and the
 * parser places it when the collection is pushed. More than one collection
 * may open in a row - spec example 2.24 tags a sequence and the mapping in
 * its first entry - so a property that is still an own-line one after the
 * first push goes back into the slot and waits for the next. One left over
 * when the document ends named the collection already filled, and that
 * collection has two.
 */
#include <gtest/gtest.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* 4JVG itself: top1 is legal and top2 is not, so the document is
	   refused - and refused for top2's sake, not top1's. */
	{"top1: &node1\n  &k1 key1: val1\ntop2: &node2\n  &v2 val2\n", nullptr},

	/* The legal half on its own, with the outer anchor actually used.  This
	   is what the single slot destroyed: the alias had nothing to resolve
	   to and a valid document was refused. */
	{"top1: &node1\n  &k1 key1: val1\nref: *node1\n",
	 "{\"top1\": {\"key1\": \"val1\"}, \"ref\": {\"key1\": \"val1\"}}"},
	{"top1: &node1\n  &k1 key1: val1\nref: *k1\n",
	 "{\"top1\": {\"key1\": \"val1\"}, \"ref\": \"key1\"}"},

	/* The illegal half on its own. */
	{"top2: &node2\n  &v2 val2\n", nullptr},

	/* A sequence is the other collection that can open between them. */
	{"top: &n\n  - &e 1\nref: *n\nref2: *e\n",
	 "{\"top\": [1], \"ref\": [1], \"ref2\": 1}"},

	/* Nothing opens between these two, so they are one node's. */
	{"a: &x &y 1\n", nullptr},
	{"a: !t1 !t2 1\n", nullptr},
	{"a: !!str !!int 1\n", nullptr},

	/* Tags take the same route as anchors. */
	{"top1: !!map\n  !!str key1: val1\n", "{\"top1\": {\"key1\": \"val1\"}}"},
	{"top2: !!str\n  !!int val2\n", nullptr},

	/* An anchor and a tag are different properties, so one of each is fine
	   however they are spread over the two lines. */
	{"top1: &node1 !!map\n  !!str key1: val1\nref: *node1\n",
	 "{\"top1\": {\"key1\": \"val1\"}, \"ref\": {\"key1\": \"val1\"}}"},
	{"top1: &node1\n  !!str key1: val1\nref: *node1\n",
	 "{\"top1\": {\"key1\": \"val1\"}, \"ref\": {\"key1\": \"val1\"}}"},

	/* Spec example 2.24: two collections open in a row, so two own-line
	   tags are two nodes' and the second has to wait for the second push. */
	{"--- !!seq\n- !!map\n  center: 1\n", "[{\"center\": 1}]"},

	/* ...and when only one opens, the second has nowhere to go. */
	{"top: &n\n  &m\n  - 1\n", nullptr},
	{"top: !!seq\n  !!map\n  - 1\n", nullptr},

	/* Three of a kind is beyond what any nesting can absorb. */
	{"a: &x\n  &y\n    &z k: v\n", nullptr},

	/* An empty node is still a node: the key here is the one the
	   properties introduce, so the mapping keeps the outer anchor. */
	{"top1: &node1\n  &k1 : val1\nref: *node1\n",
	 "{\"top1\": {null: \"val1\"}, \"ref\": {null: \"val1\"}}"},

	/* A property left behind by a line that never wrote its node belongs to
	   an empty one, and is reported before the next property takes the
	   slot - so only one can ever be waiting. */
	{"a: &x\n&y : 1\n", "{\"a\": null, null: 1}"},

	/* One property, written on an earlier line, still reaches its
	   collection - the behaviour the second slot must not disturb. */
	{"top1: &node1\n  key1: val1\nref: *node1\n",
	 "{\"top1\": {\"key1\": \"val1\"}, \"ref\": {\"key1\": \"val1\"}}"},
	/* ...and one written on an earlier line before a plain scalar stays on
	   the scalar, because no collection opens to take it. */
	{"a: &x\n  1\nref: *x\n", "{\"a\": 1, \"ref\": 1}"},
};

}  // namespace

TEST(YamlTwoProperties, Table) {
	for (const Case &c : kCases) {
		const std::string got = Render(c.input);
		if (c.expected) {
			EXPECT_EQ(got, c.expected) << "input: " << c.input;
		} else {
			EXPECT_EQ(got, "") << "should have been refused: " << c.input;
		}
	}
}

/* The message says which property was doubled, so a document with both kinds
   of mistake does not report the wrong one. */
TEST(YamlTwoProperties, TheMessageNamesTheProperty) {
	struct { const char *input; const char *message; } cases[] = {
		{"a: &x &y 1\n", "Node has more than one anchor"},
		{"a: !t1 !t2 1\n", "Node has more than one tag"},
		{"top2: &node2\n  &v2 val2\n", "Node has more than one anchor"},
		{"top2: !!str\n  !!int val2\n", "Node has more than one tag"},
	};
	for (const auto &c : cases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.input, strlen(c.input), nullptr, &err);
		ASSERT_EQ(doc, nullptr) << "accepted: " << c.input;
		EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
		ASSERT_NE(err.message, nullptr) << "input: " << c.input;
		EXPECT_STREQ(err.message, c.message) << "input: " << c.input;
	}
}

/* gtext_yaml_parse_all() reaches the end of a document by a different road:
   the DOCUMENT_END that catches this in gtext_yaml_parse() never reaches the
   per-document parser, so 4JVG went on being accepted there after the
   single-document path refused it. */
TEST(YamlTwoProperties, TheMultiDocumentParserRefusesItToo) {
	const char *input =
		"top1: &node1\n  &k1 key1: val1\ntop2: &node2\n  &v2 val2\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	size_t count = 0;
	GTEXT_YAML_Document **docs =
		gtext_yaml_parse_all(input, strlen(input), &count, nullptr, &err);
	EXPECT_EQ(docs, nullptr);
	if (docs) {
		for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
		free(docs);
	}
}

/* ...and the document after a bad one is not parsed as though nothing
   happened. */
TEST(YamlTwoProperties, ABadDocumentStopsTheStream) {
	const char *input = "top2: &node2\n  &v2 val2\n---\nfine: 1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	size_t count = 0;
	GTEXT_YAML_Document **docs =
		gtext_yaml_parse_all(input, strlen(input), &count, nullptr, &err);
	EXPECT_EQ(docs, nullptr);
	if (docs) {
		for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
		free(docs);
	}
}

/* The properties arrive over two feeds as readily as one: the second slot is
   stream state like the first, and survives a chunk boundary between them. */
TEST(YamlTwoProperties, TheSetAsidePropertySurvivesAChunkBoundary) {
	const char *input = "top1: &node1\n  &k1 key1: val1\nref: *node1\n";
	const size_t len = strlen(input);
	const std::string whole = Render(input);
	ASSERT_NE(whole, "");

	/* Split at every byte; a one-shot parse of each half-and-half must give
	   what the whole gave, because the DOM parser reassembles the feed. */
	for (size_t split = 1; split < len; split++) {
		std::string joined(input, split);
		joined.append(input + split, len - split);
		EXPECT_EQ(Render(joined.c_str()), whole) << "split at " << split;
	}
}
