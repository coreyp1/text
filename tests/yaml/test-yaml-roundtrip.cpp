/**
 * @file test-yaml-roundtrip.cpp
 * @brief parse -> write -> parse has to give the document back.
 *
 * yaml-test-suite is a corpus of inputs and tests no writer at all, so the
 * whole write side of this module went unmeasured until the suite was run
 * backwards - and the writer turned out to be far behind the parser: a
 * resolved tag went out as bare text, a non-scalar key as a sequence entry,
 * a block scalar with no chomping indicator and no indentation indicator, an
 * empty node as "~", an alias key with its colon stuck to it.  None of that
 * is visible to a test that only reads.
 *
 * `make conformance-roundtrip` scores the same property over all 282 suite
 * documents the parser accepts, but that target needs the network on first
 * use and is not what anybody types before committing.  These are the shapes
 * that were actually wrong, kept where `make test` will run them.
 *
 * Three writers are checked, because there are three: the DOM writer in its
 * default flow style, the same writer asked for block style, and the
 * streaming writer driven from the composed event stream.  They used to be
 * two separate implementations of the same rules and had drifted apart.
 */

#include <gtest/gtest.h>
#include <string.h>

#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>
}

namespace {

/* The composed event stream, in the same shape the conformance runner
 * prints, minus the scalar style: a parse-write cycle is semantically
 * faithful, not textually faithful, and the writer chooses its own styles.
 * Everything else - structure, order, anchors, tags, the text of every
 * scalar - has to come back. */
GTEXT_YAML_Status record(const GTEXT_YAML_Node_Event *ev, void *user) {
	std::string *out = (std::string *)user;
	switch (ev->type) {
	case GTEXT_YAML_NODE_EVENT_STREAM_START:   *out += "+STR\n"; break;
	case GTEXT_YAML_NODE_EVENT_STREAM_END:     *out += "-STR\n"; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_START: *out += "+DOC\n"; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_END:   *out += "-DOC\n"; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_END:   *out += "-SEQ\n"; break;
	case GTEXT_YAML_NODE_EVENT_MAPPING_END:    *out += "-MAP\n"; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_START:
	case GTEXT_YAML_NODE_EVENT_MAPPING_START:
		*out += (ev->type == GTEXT_YAML_NODE_EVENT_SEQUENCE_START)
			? "+SEQ" : "+MAP";
		if (ev->anchor) { *out += " &"; *out += ev->anchor; }
		if (ev->tag) { *out += " <"; *out += ev->tag; *out += ">"; }
		*out += "\n";
		break;
	case GTEXT_YAML_NODE_EVENT_SCALAR:
		*out += "=VAL";
		if (ev->anchor) { *out += " &"; *out += ev->anchor; }
		if (ev->tag) { *out += " <"; *out += ev->tag; *out += ">"; }
		*out += " ";
		out->append(ev->value, ev->value_len);
		*out += "\n";
		break;
	case GTEXT_YAML_NODE_EVENT_ALIAS:
		*out += "=ALI *";
		*out += ev->value ? ev->value : "";
		*out += "\n";
		break;
	}
	return GTEXT_YAML_OK;
}

GTEXT_YAML_Parse_Options keep_every_key() {
	GTEXT_YAML_Parse_Options o = gtext_yaml_parse_options_default();
	o.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;
	return o;
}

/* JSON is the by-value view: two documents that render the same JSON hold
 * the same data, whatever spelling either was written in. */
bool to_json(const std::string &yaml, std::string *out, std::string *why) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = keep_every_key();
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(
		yaml.c_str(), yaml.size(), &count, &opts, &err);
	if (!docs) {
		*why = err.message ? err.message : "(no message)";
		return false;
	}
	out->clear();
	/* The same settings the conformance runner uses: aliases resolved,
	   merge keys applied, non-string keys coerced. */
	GTEXT_YAML_To_JSON_Options jopts = gtext_yaml_to_json_options_default();
	jopts.allow_resolved_aliases = true;
	jopts.allow_merge_keys = true;
	jopts.coerce_keys_to_strings = true;
	for (size_t i = 0; i < count; i++) {
		GTEXT_JSON_Value *jv = NULL;
		memset(&err, 0, sizeof(err));
		if (gtext_yaml_to_json_with_options(docs[i], &jv, &jopts, &err)
				!= GTEXT_YAML_OK) {
			*out += "(tojson failed)\n";
			continue;
		}
		if (!jv) { *out += "null\n"; continue; }
		GTEXT_JSON_Sink sink;
		if (gtext_json_sink_buffer(&sink) == GTEXT_JSON_OK) {
			GTEXT_JSON_Error jerr;
			memset(&jerr, 0, sizeof(jerr));
			if (gtext_json_write_value(&sink, NULL, jv, &jerr) == GTEXT_JSON_OK) {
				out->append(gtext_json_sink_buffer_data(&sink),
					gtext_json_sink_buffer_size(&sink));
			}
			*out += "\n";
			gtext_json_sink_buffer_free(&sink);
		}
		gtext_json_free(jv);
	}
	for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
	free(docs);
	return true;
}

bool to_events(const std::string &yaml, std::string *out, std::string *why) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = keep_every_key();
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(
		yaml.c_str(), yaml.size(), &count, &opts, &err);
	if (!docs) {
		*why = err.message ? err.message : "(no message)";
		return false;
	}
	out->clear();
	GTEXT_YAML_Status st = gtext_yaml_stream_walk(docs, count, record, out);
	for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
	free(docs);
	if (st != GTEXT_YAML_OK) { *why = "walk failed"; return false; }
	return true;
}

enum Writer { DOM_FLOW, DOM_BLOCK, STREAMING };

struct Relay {
	GTEXT_YAML_Writer *writer;
	GTEXT_YAML_Status status;
};

GTEXT_YAML_Status relay(const GTEXT_YAML_Node_Event *ev, void *user) {
	Relay *r = (Relay *)user;
	GTEXT_YAML_Event out;
	memset(&out, 0, sizeof(out));
	switch (ev->type) {
	case GTEXT_YAML_NODE_EVENT_STREAM_START:
		out.type = GTEXT_YAML_EVENT_STREAM_START; break;
	case GTEXT_YAML_NODE_EVENT_STREAM_END:
		out.type = GTEXT_YAML_EVENT_STREAM_END; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_START:
		out.type = GTEXT_YAML_EVENT_DOCUMENT_START; break;
	case GTEXT_YAML_NODE_EVENT_DOCUMENT_END:
		out.type = GTEXT_YAML_EVENT_DOCUMENT_END; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_START:
		out.type = GTEXT_YAML_EVENT_SEQUENCE_START; break;
	case GTEXT_YAML_NODE_EVENT_SEQUENCE_END:
		out.type = GTEXT_YAML_EVENT_SEQUENCE_END; break;
	case GTEXT_YAML_NODE_EVENT_MAPPING_START:
		out.type = GTEXT_YAML_EVENT_MAPPING_START; break;
	case GTEXT_YAML_NODE_EVENT_MAPPING_END:
		out.type = GTEXT_YAML_EVENT_MAPPING_END; break;
	case GTEXT_YAML_NODE_EVENT_SCALAR:
		out.type = GTEXT_YAML_EVENT_SCALAR;
		out.data.scalar.ptr = ev->value;
		out.data.scalar.len = ev->value_len;
		out.scalar_style = ev->scalar_style;
		break;
	case GTEXT_YAML_NODE_EVENT_ALIAS:
		out.type = GTEXT_YAML_EVENT_ALIAS;
		out.data.alias_name = ev->value;
		break;
	}
	out.anchor = ev->anchor;
	out.tag = ev->tag;
	r->status = gtext_yaml_writer_event(r->writer, &out);
	return r->status;
}

/* Write a document back out.  Returns false only when the writer refused;
 * an empty stream legitimately writes nothing. */
bool write_back(const std::string &yaml, Writer which,
		std::string *out, std::string *why) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options popts = keep_every_key();
	size_t count = 0;
	GTEXT_YAML_Document **docs = gtext_yaml_parse_all(
		yaml.c_str(), yaml.size(), &count, &popts, &err);
	if (!docs) { *why = err.message ? err.message : "(no message)"; return false; }

	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	wopts.trailing_newline = true;
	if (which == DOM_BLOCK) {
		wopts.pretty = true;
		wopts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
	}

	GTEXT_YAML_Sink sink;
	if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) {
		*why = "no sink";
		for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
		free(docs);
		return false;
	}

	GTEXT_YAML_Status st;
	if (which == STREAMING) {
		Relay r;
		r.status = GTEXT_YAML_OK;
		r.writer = gtext_yaml_writer_new(sink, &wopts);
		st = r.writer ? gtext_yaml_stream_walk(docs, count, relay, &r)
			: GTEXT_YAML_E_OOM;
		if (st == GTEXT_YAML_OK) st = gtext_yaml_writer_finish(r.writer);
		gtext_yaml_writer_free(r.writer);
	} else if (count == 1) {
		st = gtext_yaml_write_document(docs[0], &sink, &wopts);
	} else {
		st = gtext_yaml_write_documents(docs, count, &sink, &wopts);
	}

	if (st == GTEXT_YAML_OK) {
		const char *data = gtext_yaml_sink_buffer_data(&sink);
		size_t len = gtext_yaml_sink_buffer_size(&sink);
		out->assign(data ? data : "", len);
	} else {
		*why = "the writer refused the document";
	}
	gtext_yaml_sink_buffer_free(&sink);
	for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
	free(docs);
	return st == GTEXT_YAML_OK;
}

const char *writer_name(Writer w) {
	switch (w) {
	case DOM_FLOW:  return "the DOM writer, flow style";
	case DOM_BLOCK: return "the DOM writer, block style";
	default:        return "the streaming writer";
	}
}

/* Every one of these was written wrongly by at least one of the three
 * writers, or read wrongly on the way back in. */
const char *const kDocuments[] = {
	/* A tag a %TAG directive resolved: written as bare text, it came back
	   as a mapping key or split on the comma. */
	"%TAG !e! tag:example.com,2000:app/\n---\n- !local foo\n- !!str bar\n- !e!tag%21 baz\n",
	"%TAG ! tag:clarkevans.com,2002:\n--- !shape\n- !circle\n  center: &O {x: 73, y: 129}\n  radius: 7\n- !line\n  start: *O\n",
	"!<tag:yaml.org,2002:str> foo :\n  !<!bar> baz\n",
	"%TAG !yaml! tag:yaml.org,2002:\n---\n!yaml!str \"foo\"\n",
	"--- !<tag:clarkevans.com,2002:invoice>\ninvoice: 34843\ndate: 2001-01-23\n",
	"- !!map\n  foo : bar\n",

	/* Block scalars: chomping, the indentation indicator, and folding. */
	"- |\n detected\n- >\n \n  \n  # detected\n- |1\n  explicit\n- >\n detected\n",
	"--- |-\n ab\n \n \n...\n",
	"--- >\n ab\n cd\n \n ef\n\n\n gh\n",
	">-\n  trimmed\n  \n \n\n  as\n  space\n",
	"foo: |-\n \tbar\n",
	"- |2-\n  explicit indent and chomp\n- |-2\n  chomp and explicit indent\n",
	"---\na: >2\n   more indented\n  regular\nb: >2\n\n\n   more indented\n  regular\n",
	"a: |+\n  x\n\n\nb: 1\n",

	/* Single quotes cannot hold a line break: it folds away to a space. */
	"---\na: '\n  '\nb: '  \n  '\nc: \"\n  \"\ne: '\n\n  '\ng: '\n\n\n  '\n",
	"' 1st non-empty\n\n 2nd non-empty \n\t3rd non-empty '\n",

	/* An alias or a property standing where a ":" follows it. */
	"&a a: &b b\n*b : *a\n",
	"\"top1\" : \n  \"key1\" : &alias1 scalar1\ntop3: &node3 \n  *alias1 : scalar3\n",
	"- !!str\n-\n  !!null : a\n  b: !!str\n- !!str : !!null\n",
	"- &a\n- a\n-\n  &a : a\n  b: &b\n-\n  &c : &a\n",

	/* The empty node of 7.2, in every position that has a spelling for it. */
	"---\na: &anchor\nb: *anchor\n",
	":\n",
	"-\n",
	"- :\n",
	"{\nunquoted : \"separate\",\nhttp://foo.com,\nomitted value:,\n}\n",
	"? a\n  true\n: null\n  d\n? e\n  42\n",

	/* The non-specific tag, which is a whole property on its own. */
	"a: !\nb: 2\n",
	"- !\n- x\n",
	"{a: !}\n",
	"[!]\n",
	"! a\n",

	/* Empty collections, which have to stay on the line that introduced
	   them rather than start one of their own in column zero. */
	"---\nnested sequences:\n- - - []\n- - - {}\nkey1: []\nkey2: {}\n",
	"- [a, b , c ]\n- { \"a\"  : b\n   , c : 'd' ,\n   e   : \"f\"\n  }\n- [      ]\n",

	/* A key whose node carries properties, under an empty value. */
	"---\na: 1\n? b\n&anchor c: 3\n",

	/* Streams with no documents in them, and documents with no content. */
	"# Comment only.\n",
	"...\n",
	"\n",
	"%YAML 1.1\n---\n",
	"---\n---\n",

	/* And a plain ordinary document, so a regression that broke everything
	   is not reported only in the corners. */
	"invoice: 34843\nbill-to: &id001\n  given: Chris\n  address:\n    lines: |\n      458 Walkman Dr.\n      Suite #292\n    city: Royal Oak\nship-to: *id001\nproduct:\n  - sku: BL394D\n    quantity: 4\n",
};

}  // namespace

TEST(YamlRoundTrip, NoWriterLosesData) {
	for (const char *doc : kDocuments) {
		const std::string input(doc);
		std::string before, why;
		ASSERT_TRUE(to_json(input, &before, &why))
			<< "the parser refused a document this test assumes it takes: "
			<< why << "\n" << input;

		for (Writer w : {DOM_FLOW, DOM_BLOCK, STREAMING}) {
			std::string written;
			ASSERT_TRUE(write_back(input, w, &written, &why))
				<< writer_name(w) << ": " << why << "\n--- input ---\n" << input;

			std::string after;
			ASSERT_TRUE(to_json(written, &after, &why))
				<< writer_name(w) << " wrote something that does not parse: "
				<< why << "\n--- input ---\n" << input
				<< "--- written ---\n" << written;

			EXPECT_EQ(before, after)
				<< writer_name(w) << " changed the document\n"
				<< "--- input ---\n" << input
				<< "--- written ---\n" << written;
		}
	}
}

/* Block style is the strict case: everything survives, including the text of
 * every scalar and every anchor and tag.  The two flow styles cannot quite
 * reach it, because ns-flow-seq-entry has no empty alternative - an entry
 * that is the empty node with no properties has to be written "~" there, and
 * "~" is a scalar the document did not hold.  That is the whole of the
 * difference, and it is why this test is block-only. */
TEST(YamlRoundTrip, BlockStyleKeepsEverySpelling) {
	for (const char *doc : kDocuments) {
		const std::string input(doc);
		std::string before, after, written, why;
		ASSERT_TRUE(to_events(input, &before, &why)) << why << "\n" << input;
		ASSERT_TRUE(write_back(input, DOM_BLOCK, &written, &why))
			<< why << "\n" << input;
		ASSERT_TRUE(to_events(written, &after, &why))
			<< why << "\n--- input ---\n" << input
			<< "--- written ---\n" << written;
		EXPECT_EQ(before, after)
			<< "--- input ---\n" << input << "--- written ---\n" << written;
	}
}

/* What the fixes above actually look like on the page.  The property tests
 * would pass on any spelling that happens to read back the same; these say
 * which spelling the writer picks, so a change of mind is visible. */
TEST(YamlRoundTrip, TheSpellingsThatWereWrong) {
	struct { const char *in; const char *want; } cases[] = {
		/* A resolved tag goes out verbatim, not as bare text - which used
		   to read back as a mapping key, or split on the comma. */
		{"%TAG !e! tag:example.com,2000:app/\n---\n!e!foo \"bar\"\n",
		 "!<tag:example.com,2000:app/foo> \"bar\"\n"},
		/* A tag in the standard namespace has one spelling in this DOM,
		   whichever of the three ways it was written. */
		{"!<tag:yaml.org,2002:str> x\n", "!!str x\n"},
		/* ns-uri-char admits "!", so a suffix that was written "%21" comes
		   back as itself inside the brackets. */
		{"%TAG !e! tag:example.com,2000:app/\n---\n!e!tag%21 baz\n",
		 "!<tag:example.com,2000:app/tag!> baz\n"},
		/* Chomping: clip, strip, keep.  The writer used to emit "|" for all
		   three, so a value with no trailing break gained one and a value
		   with three kept one. */
		{"a: |\n  x\n", "a: |\n    x\n"},
		{"a: |-\n  x\n", "a: |-\n    x\n"},
		{"a: |+\n  x\n\n\n", "a: |+\n    x\n\n\n"},
		/* A first line that begins with a space needs the indentation
		   indicator, or auto-detection takes the space for indentation. */
		{"a: |2\n   x\n  y\n", "a: |4\n     x\n    y\n"},
		/* An alias key is separated from its colon: ns-anchor-char stops
		   only at a flow indicator, so "*b:" would name the anchor "b:". */
		{"&b b: 1\n*b : 2\n", "&b b: 1\n*b : 2\n"},
		/* An empty node is written as nothing, not as "~". */
		{"a:\nb: 1\n", "a:\nb: 1\n"},
		/* An empty collection stays on the line that introduced it, rather
		   than starting one of its own in column zero. */
		{"a: []\nb: {}\n", "a: []\nb: {}\n"},
	};

	for (const auto &c : cases) {
		std::string written, why;
		ASSERT_TRUE(write_back(c.in, DOM_BLOCK, &written, &why))
			<< why << "\n" << c.in;
		EXPECT_EQ(written, std::string(c.want)) << "for input:\n" << c.in;
	}
}
