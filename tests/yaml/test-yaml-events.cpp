/**
 * @file test-yaml-events.cpp
 * @brief Tests for the composed node-event walk.
 */

#include <gtest/gtest.h>
#include <string.h>

#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/* The events, rendered one per line.  The notation is yaml-test-suite's,
 * because it is compact, it is the one tools/conformance/yaml_event_suite.c
 * prints, and a reader who wants to know what a document should produce can
 * look the case up in the suite. */
struct Recorder {
	std::string text;
	size_t stop_after = 0;   /* 0 = never stop */
	size_t seen = 0;
};

GTEXT_YAML_Status record(const GTEXT_YAML_Node_Event *event, void *user) {
	Recorder *rec = static_cast<Recorder *>(user);
	rec->seen++;

	std::string line;
	switch (event->type) {
	case GTEXT_YAML_NODE_EVENT_STREAM_START: line = "+STR"; break;
	case GTEXT_YAML_NODE_EVENT_STREAM_END:   line = "-STR"; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_START:
		line = event->explicit_marker ? "+DOC ---" : "+DOC"; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_END:
		line = event->explicit_marker ? "-DOC ..." : "-DOC"; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_END: line = "-SEQ"; break;
	case GTEXT_YAML_NODE_EVENT_MAPPING_END:  line = "-MAP"; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_START:
	case GTEXT_YAML_NODE_EVENT_MAPPING_START:
		line = event->type == GTEXT_YAML_NODE_EVENT_SEQUENCE_START ? "+SEQ" : "+MAP";
		if (event->flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) {
			line += event->type == GTEXT_YAML_NODE_EVENT_SEQUENCE_START ? " []" : " {}";
		}
		if (event->anchor) { line += " &"; line += event->anchor; }
		if (event->tag) { line += " <"; line += event->tag; line += ">"; }
		break;
	case GTEXT_YAML_NODE_EVENT_SCALAR:
		line = "=VAL";
		if (event->anchor) { line += " &"; line += event->anchor; }
		if (event->tag) { line += " <"; line += event->tag; line += ">"; }
		line += " ";
		switch (event->scalar_style) {
		case GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED: line += "'"; break;
		case GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED: line += "\""; break;
		case GTEXT_YAML_SCALAR_STYLE_LITERAL: line += "|"; break;
		case GTEXT_YAML_SCALAR_STYLE_FOLDED: line += ">"; break;
		default: line += ":"; break;
		}
		line.append(event->value, event->value_len);
		break;
	case GTEXT_YAML_NODE_EVENT_ALIAS:
		line = "=ALI *";
		line.append(event->value, event->value_len);
		break;
	}
	rec->text += line;
	rec->text += "\n";

	if (rec->stop_after && rec->seen >= rec->stop_after) return GTEXT_YAML_E_STATE;
	return GTEXT_YAML_OK;
}

/* Parse a whole stream and walk it, returning the rendered events. */
std::string walk(const char *yaml) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	size_t count = 0;
	GTEXT_YAML_Document **docs =
		gtext_yaml_parse_all(yaml, strlen(yaml), &count, NULL, &error);
	if (!docs) return std::string("PARSE FAILED: ")
		+ (error.message ? error.message : "unknown");

	Recorder rec;
	GTEXT_YAML_Status status = gtext_yaml_stream_walk(docs, count, record, &rec);
	for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
	free(docs);
	if (status != GTEXT_YAML_OK) return "WALK FAILED";
	return rec.text;
}

}  // namespace

TEST(YamlEvents, AMappingOfScalarsWalksInWrittenOrder) {
	EXPECT_EQ(walk("name: Mark\nhr: 65\n"),
		"+STR\n"
		"+DOC\n"
		"+MAP\n"
		"=VAL :name\n"
		"=VAL :Mark\n"
		"=VAL :hr\n"
		"=VAL :65\n"
		"-MAP\n"
		"-DOC\n"
		"-STR\n");
}

TEST(YamlEvents, TheTextIsTheOneThatWasWrittenNotTheResolvedValue) {
	/* 65 resolves to an integer and 0x1F to 31, but the event reports the
	   scalar as the document spells it - the style and the text are what an
	   emitter would have to reproduce. */
	EXPECT_EQ(walk("- 0x1F\n- 'yes'\n"),
		"+STR\n+DOC\n+SEQ\n=VAL :0x1F\n=VAL 'yes\n-SEQ\n-DOC\n-STR\n");
}

TEST(YamlEvents, FlowAndBlockAreDifferentDocuments) {
	EXPECT_EQ(walk("a: {b: c}\n"),
		"+STR\n+DOC\n+MAP\n=VAL :a\n+MAP {}\n=VAL :b\n=VAL :c\n-MAP\n-MAP\n-DOC\n-STR\n");
	EXPECT_EQ(walk("a:\n  b: c\n"),
		"+STR\n+DOC\n+MAP\n=VAL :a\n+MAP\n=VAL :b\n=VAL :c\n-MAP\n-MAP\n-DOC\n-STR\n");
	EXPECT_EQ(walk("a: [1]\n"),
		"+STR\n+DOC\n+MAP\n=VAL :a\n+SEQ []\n=VAL :1\n-SEQ\n-MAP\n-DOC\n-STR\n");
}

TEST(YamlEvents, TheDocumentMarkersSayWhetherTheyWereWritten) {
	EXPECT_EQ(walk("a: 1\n"), "+STR\n+DOC\n+MAP\n=VAL :a\n=VAL :1\n-MAP\n-DOC\n-STR\n");
	EXPECT_EQ(walk("---\na: 1\n"),
		"+STR\n+DOC ---\n+MAP\n=VAL :a\n=VAL :1\n-MAP\n-DOC\n-STR\n");
	EXPECT_EQ(walk("---\na: 1\n...\n"),
		"+STR\n+DOC ---\n+MAP\n=VAL :a\n=VAL :1\n-MAP\n-DOC ...\n-STR\n");
}

TEST(YamlEvents, AMarkerClosingOneDocumentAndOpeningTheNextIsOnlyAStart) {
	/* "---" ends the document before it, but it is not a "..." and the end it
	   produces was not written. */
	EXPECT_EQ(walk("a\n---\nb\n"),
		"+STR\n"
		"+DOC\n=VAL :a\n-DOC\n"
		"+DOC ---\n=VAL :b\n-DOC\n"
		"-STR\n");
}

TEST(YamlEvents, AnchorsAndTagsRideOnTheirOwnNode) {
	EXPECT_EQ(walk("--- !!set\n? a\n"),
		"+STR\n+DOC ---\n+MAP <!!set>\n=VAL :a\n=VAL :\n-MAP\n-DOC\n-STR\n");
	EXPECT_EQ(walk("&seq\n- &item a\n"),
		"+STR\n+DOC\n+SEQ &seq\n=VAL &item :a\n-SEQ\n-DOC\n-STR\n");
}

TEST(YamlEvents, TheTagIsTheSpellingTheNodeCarries) {
	/* Not expanded to a URI: "!!str" and "tag:yaml.org,2002:str" are the same
	   tag, and choosing between the two spellings is the caller's business.
	   tools/conformance/yaml_event_suite.c makes the other choice. */
	EXPECT_EQ(walk("!!str 1\n"), "+STR\n+DOC\n=VAL <!!str> :1\n-DOC\n-STR\n");
	EXPECT_EQ(walk("!local 1\n"), "+STR\n+DOC\n=VAL <!local> :1\n-DOC\n-STR\n");
}

TEST(YamlEvents, AnOmittedValueIsStillANode) {
	/* "a:" with nothing after it is a pair whose value is the empty node
	   (7.2).  Leaving the event out would make the walk disagree with the
	   mapping about how many pairs it has. */
	EXPECT_EQ(walk("a:\n"), "+STR\n+DOC\n+MAP\n=VAL :a\n=VAL :\n-MAP\n-DOC\n-STR\n");
}

TEST(YamlEvents, AnAliasIsAnEventAndIsNotFollowed) {
	EXPECT_EQ(walk("a: &x 1\nb: *x\n"),
		"+STR\n+DOC\n+MAP\n=VAL :a\n=VAL &x :1\n=VAL :b\n=ALI *x\n-MAP\n-DOC\n-STR\n");
}

TEST(YamlEvents, ASelfReferentialDocumentWalksInFiniteTime) {
	/* The alias points at the sequence containing it.  A walk that expanded
	   aliases would not return; this one reports the alias and stops.  The
	   assertion is the whole test, so it does not skip: a build where the
	   parser stopped accepting this has to say so rather than go quiet. */
	EXPECT_EQ(walk("&a [*a]\n"),
		"+STR\n+DOC\n+SEQ [] &a\n=ALI *a\n-SEQ\n-DOC\n-STR\n");
	EXPECT_EQ(walk("&a {b: *a}\n"),
		"+STR\n+DOC\n+MAP {} &a\n=VAL :b\n=ALI *a\n-MAP\n-DOC\n-STR\n");
}

TEST(YamlEvents, DocumentWalkBracketsOnlyTheDocument) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	const char *yaml = "a: 1\n";
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	Recorder rec;
	EXPECT_EQ(gtext_yaml_document_walk(doc, record, &rec), GTEXT_YAML_OK);
	EXPECT_EQ(rec.text, "+DOC\n+MAP\n=VAL :a\n=VAL :1\n-MAP\n-DOC\n");
	gtext_yaml_free(doc);
}

TEST(YamlEvents, AStreamWithNoDocumentsIsStillAStream) {
	Recorder rec;
	EXPECT_EQ(gtext_yaml_stream_walk(NULL, 0, record, &rec), GTEXT_YAML_OK);
	EXPECT_EQ(rec.text, "+STR\n-STR\n");
}

TEST(YamlEvents, TheCallbacksStatusStopsTheWalkAndIsReturned) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	const char *yaml = "a: 1\n";
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	Recorder rec;
	rec.stop_after = 3;
	EXPECT_EQ(gtext_yaml_document_walk(doc, record, &rec), GTEXT_YAML_E_STATE);
	EXPECT_EQ(rec.seen, 3u);
	EXPECT_EQ(rec.text, "+DOC\n+MAP\n=VAL :a\n");
	gtext_yaml_free(doc);
}

TEST(YamlEvents, MissingArgumentsAreRefused) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	const char *yaml = "a: 1\n";
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	Recorder rec;
	EXPECT_EQ(gtext_yaml_document_walk(NULL, record, &rec), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(gtext_yaml_document_walk(doc, NULL, &rec), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(gtext_yaml_stream_walk(&doc, 1, NULL, &rec), GTEXT_YAML_E_INVALID);
	/* A NULL array with a non-zero count is a lie about what is there. */
	EXPECT_EQ(gtext_yaml_stream_walk(NULL, 1, record, &rec), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(rec.text, "");
	gtext_yaml_free(doc);
}

TEST(YamlEvents, FlowStyleIsAskableOfTheNodeToo) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	const char *yaml = "block:\n  - 1\nflow: [1]\n";
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	GTEXT_YAML_Flow_Style style = GTEXT_YAML_FLOW_STYLE_AUTO;
	ASSERT_TRUE(gtext_yaml_node_flow_style(root, &style));
	EXPECT_EQ(style, GTEXT_YAML_FLOW_STYLE_BLOCK);

	style = GTEXT_YAML_FLOW_STYLE_AUTO;
	ASSERT_TRUE(gtext_yaml_node_flow_style(gtext_yaml_mapping_get(root, "block"), &style));
	EXPECT_EQ(style, GTEXT_YAML_FLOW_STYLE_BLOCK);

	style = GTEXT_YAML_FLOW_STYLE_AUTO;
	ASSERT_TRUE(gtext_yaml_node_flow_style(gtext_yaml_mapping_get(root, "flow"), &style));
	EXPECT_EQ(style, GTEXT_YAML_FLOW_STYLE_FLOW);

	/* A scalar was not written in either style, and says so by refusing. */
	EXPECT_FALSE(gtext_yaml_node_flow_style(gtext_yaml_mapping_get(root, "block"), NULL));
	gtext_yaml_free(doc);
}

TEST(YamlEvents, ACollectionBuiltRatherThanParsedHasNoWrittenStyle) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, &error);
	ASSERT_NE(doc, nullptr);
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	ASSERT_NE(seq, nullptr);

	GTEXT_YAML_Flow_Style style = GTEXT_YAML_FLOW_STYLE_BLOCK;
	ASSERT_TRUE(gtext_yaml_node_flow_style(seq, &style));
	EXPECT_EQ(style, GTEXT_YAML_FLOW_STYLE_AUTO);
	gtext_yaml_free(doc);
}

TEST(YamlEvents, TheDocumentMarkersAreAskableOfTheDocumentToo) {
	struct { const char *yaml; bool start; bool end; } cases[] = {
		{"a: 1\n", false, false},
		{"---\na: 1\n", true, false},
		{"---\na: 1\n...\n", true, true},
	};
	for (const auto &c : cases) {
		GTEXT_YAML_Error error;
		memset(&error, 0, sizeof(error));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.yaml, strlen(c.yaml), NULL, &error);
		ASSERT_NE(doc, nullptr) << c.yaml;
		EXPECT_EQ(gtext_yaml_document_has_explicit_start(doc), c.start) << c.yaml;
		EXPECT_EQ(gtext_yaml_document_has_explicit_end(doc), c.end) << c.yaml;
		gtext_yaml_free(doc);
	}
	EXPECT_FALSE(gtext_yaml_document_has_explicit_start(NULL));
	EXPECT_FALSE(gtext_yaml_document_has_explicit_end(NULL));
}

/* A property written at the end of a line introduces whatever the next line
 * opens.  When that is a block mapping the parser has to hold the property
 * until the ":" that opens the mapping arrives, and it used to do so only
 * when the mapping's first key was a plain scalar - the one shape where the
 * key event and the mapping-opening ":" are adjacent.  An alias key or a flow
 * collection key put other events in between and the property was lost, or
 * worse, drifted onto a later node.  Both are suite cases that the event
 * stream is the only way to ask about: the composed value is identical
 * either way, so nothing but the anchor's position distinguishes them. */
TEST(YamlEvents, AnOwnLinePropertyReachesAMappingWhoseFirstKeyIsNotAScalar) {
	/* Suite case 26DV.  &node3 is the nested mapping's; before the fix it
	 * came out on scalar3, which says the wrong thing rather than nothing. */
	EXPECT_EQ(walk("a: &x scalar1\ntop3: &node3\n  *x : scalar3\n"),
		"+STR\n"
		"+DOC\n"
		"+MAP\n"
		"=VAL :a\n"
		"=VAL &x :scalar1\n"
		"=VAL :top3\n"
		"+MAP &node3\n"
		"=ALI *x\n"
		"=VAL :scalar3\n"
		"-MAP\n"
		"-MAP\n"
		"-DOC\n"
		"-STR\n");

	/* Suite case 6BFJ.  &mapping waits through five events - the sequence
	 * start, its three entries and its end - before the ":" claims it. */
	EXPECT_EQ(walk("---\n&mapping\n&key [ &item a, b, c ]: value\n"),
		"+STR\n"
		"+DOC ---\n"
		"+MAP &mapping\n"
		"+SEQ [] &key\n"
		"=VAL &item :a\n"
		"=VAL :b\n"
		"=VAL :c\n"
		"-SEQ\n"
		"=VAL :value\n"
		"-MAP\n"
		"-DOC\n"
		"-STR\n");

	/* A tag travels the same way, and so does a mapping key. */
	EXPECT_EQ(walk("a: &x 1\ntop: !!map\n  *x : v\n"),
		"+STR\n"
		"+DOC\n"
		"+MAP\n"
		"=VAL :a\n"
		"=VAL &x :1\n"
		"=VAL :top\n"
		"+MAP <!!map>\n"
		"=ALI *x\n"
		"=VAL :v\n"
		"-MAP\n"
		"-MAP\n"
		"-DOC\n"
		"-STR\n");
}

/* The other reading of the same input.  A property held for a collection that
 * never opens named the node in front of it after all, and one written on an
 * alias named a node that may not have it (7.1).  Both are refused, and the
 * message says which, because "more than one anchor" is a confusing thing to
 * read about a document that has only one. */
TEST(YamlEvents, AHeldPropertyThatNoCollectionClaimsIsRefused) {
	struct Case { const char *yaml; const char *message; };
	const Case cases[] = {
		/* Suite case 4JVG: no collection opens, so both are val2's. */
		{"top2: &node2\n  &v2 val2\n", "Node has more than one anchor"},
		{"top2: !!str\n  !!int val2\n", "Node has more than one tag"},
		/* The alias is the value, not a key, so nothing opens and &node3
		 * was written on an alias. */
		{"a: &x 1\ntop: &node3\n  *x\n", "An alias node may not carry an anchor"},
		{"a: &x 1\nb: &y *x\n", "An alias node may not carry an anchor"},
		{"a: &x 1\nb: !!str *x\n", "An alias node may not carry a tag"},
	};

	for (const Case &c : cases) {
		GTEXT_YAML_Error error;
		memset(&error, 0, sizeof(error));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.yaml, strlen(c.yaml), NULL, &error);
		EXPECT_EQ(doc, nullptr) << c.yaml;
		if (doc) { gtext_yaml_free(doc); continue; }
		ASSERT_NE(error.message, nullptr) << c.yaml;
		EXPECT_STREQ(error.message, c.message) << c.yaml;
	}
}
