/**
 * @file
 *
 * libFuzzer harness for the YAML *writers*.
 *
 * fuzz_yaml.cpp parses and walks and never calls a writer, so until this
 * existed the most productive tool applied to this module had never been
 * pointed at half of it.  Four defects were found by hand that this would
 * have found: a character 5.1 forbids written raw, an anchor name written
 * without being checked, a tag gaining a layer of percent-encoding on every
 * round trip, and the streaming writer dropping the %TAG directive that said
 * what a tag spelling meant.
 *
 * The property is the one all four broke:
 *
 *     if the writer says OK, the bytes it wrote must parse,
 *     and must hold the same values.
 *
 * Two families of input reach the writers here, and they are not the same:
 *
 *   - documents that came from parsing, which is what a round trip over
 *     yaml-test-suite measures; and
 *   - documents built through the DOM API, which is where the interesting
 *     failures were, because a corpus of YAML text can only carry values the
 *     parser accepts.  A DEL never reaches the writer from the first
 *     direction and is ordinary in the second.
 *
 * Build with: make fuzz-yaml-writer     Run: make fuzz-run-yaml-writer
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>
}

namespace {

/** The JSON of a document, as the comparable form of "the same values". */
std::string values_of(const GTEXT_YAML_Document * doc, bool * ok) {
  GTEXT_JSON_Value * json = nullptr;
  *ok = false;
  if (gtext_yaml_to_json(doc, &json, nullptr) != GTEXT_YAML_OK || !json) {
    if (json) gtext_json_free(json);
    return std::string();
  }
  GTEXT_JSON_Sink sink;
  if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
    gtext_json_free(json);
    return std::string();
  }
  std::string out;
  GTEXT_JSON_Error jerr;
  memset(&jerr, 0, sizeof(jerr));
  if (gtext_json_write_value(&sink, nullptr, json, &jerr) == GTEXT_JSON_OK) {
    out.assign(gtext_json_sink_buffer_data(&sink),
               gtext_json_sink_buffer_size(&sink));
    *ok = true;
  }
  gtext_json_sink_buffer_free(&sink);
  gtext_json_free(json);
  return out;
}

/** Everything the writer produced, or the empty string if it refused. */
bool write_document(
    const GTEXT_YAML_Document * doc,
    const GTEXT_YAML_Write_Options * opts,
    std::string * out) {
  GTEXT_YAML_Sink sink;
  if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) return false;
  GTEXT_YAML_Status st = gtext_yaml_write_document(doc, &sink, opts);
  if (st == GTEXT_YAML_OK) {
    out->assign(gtext_yaml_sink_buffer_data(&sink),
                gtext_yaml_sink_buffer_size(&sink));
  }
  gtext_yaml_sink_buffer_free(&sink);
  return st == GTEXT_YAML_OK;
}

/* A refusal is always allowed - not every value has a YAML spelling, and
   saying so is the correct answer.  What is never allowed is claiming
   success and producing something this library cannot read back. */
void must_round_trip(const GTEXT_YAML_Document * doc, const std::string & text) {
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
  /* The question is whether this library can read back what it wrote, not
     whether the default duplicate-key policy likes the document. A DOM may
     legitimately hold two equal keys - nothing in the API prevents it - and
     writing it faithfully is the writer doing its job; refusing to read it
     again is the reader doing its own. "{: , : }" was the fuzzer's way of
     asking, and it is not a defect. */
  popts.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;
  GTEXT_YAML_Document * back =
      gtext_yaml_parse(text.data(), text.size(), &popts, &err);
  if (!back) {
    /* Printed before the trap, because an artifact is a handful of bytes that
       says nothing on its own: what makes one triageable is seeing what the
       writer produced. */
    fprintf(stderr, "the writer wrote what the parser refuses:\n  %.*s\n",
            (int)text.size(), text.data());
    gtext_yaml_error_free(&err);
    __builtin_trap();
  }
  gtext_yaml_error_free(&err);

  bool a_ok = false, b_ok = false;
  std::string a = values_of(doc, &a_ok);
  std::string b = values_of(back, &b_ok);
  gtext_yaml_free(back);
  /* Not every document converts to JSON - a non-string key, a cycle - and
     then there is nothing to compare.  Only a pair that both converted says
     anything. */
  if (a_ok && b_ok && a != b) {
    fprintf(stderr, "the writer wrote a different document:\n"
                    "  wrote  %.*s\n  before %s\n  after  %s\n",
            (int)text.size(), text.data(), a.c_str(), b.c_str());
    __builtin_trap();
  }
}

/** A cursor over the fuzzer's bytes, used to drive the DOM builder. */
struct Bytes {
  const uint8_t * p;
  size_t n;
  size_t i = 0;
  uint8_t next() { return i < n ? p[i++] : 0; }
  bool done() const { return i >= n; }
  /* A run of bytes, used raw: this is the point of the exercise, since a
     value built through the API is under no obligation to be text the parser
     would have accepted. */
  std::string chunk() {
    size_t len = next() & 0x0F;
    std::string s;
    for (size_t k = 0; k < len && !done(); k++) s.push_back((char)next());
    return s;
  }
};

/* NULL for "no property", so the writer sees both the absent and the hostile
   case. */
const char * maybe(Bytes & b, std::string * store) {
  if ((b.next() & 1) == 0) return nullptr;
  *store = b.chunk();
  return store->c_str();
}

GTEXT_YAML_Node * build(
    GTEXT_YAML_Document * doc, Bytes & b, int depth) {
  std::string tag_s, anchor_s;
  const char * tag = maybe(b, &tag_s);
  const char * anchor = maybe(b, &anchor_s);
  uint8_t kind = b.done() || depth > 6 ? 0 : (b.next() % 3);

  if (kind == 0) {
    std::string v = b.chunk();
    return gtext_yaml_node_new_scalar(doc, v.c_str(), tag, anchor);
  }
  if (kind == 1) {
    GTEXT_YAML_Node * seq = gtext_yaml_node_new_sequence(doc, tag, anchor);
    size_t count = b.next() & 0x03;
    for (size_t k = 0; k < count && !b.done() && seq; k++) {
      GTEXT_YAML_Node * child = build(doc, b, depth + 1);
      if (!child) break;
      seq = gtext_yaml_sequence_append(doc, seq, child);
    }
    return seq;
  }
  GTEXT_YAML_Node * map = gtext_yaml_node_new_mapping(doc, tag, anchor);
  size_t count = b.next() & 0x03;
  for (size_t k = 0; k < count && !b.done() && map; k++) {
    GTEXT_YAML_Node * key = build(doc, b, depth + 1);
    GTEXT_YAML_Node * value = build(doc, b, depth + 1);
    if (!key || !value) break;
    map = gtext_yaml_mapping_set(doc, map, key, value);
  }
  return map;
}

/** Documents the parser could never hand the writer. */
void fuzz_built_dom(const uint8_t * data, size_t size, bool block) {
  GTEXT_YAML_Document * doc = gtext_yaml_document_new(nullptr, nullptr);
  if (!doc) return;
  Bytes b{data, size};
  GTEXT_YAML_Node * root = build(doc, b, 0);
  if (root) gtext_yaml_document_set_root(doc, root);

  GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
  if (block) {
    wopts.pretty = true;
    wopts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
  }
  std::string text;
  if (write_document(doc, &wopts, &text)) must_round_trip(doc, text);
  gtext_yaml_free(doc);
}

/** Documents that did come from parsing - the round trip, unbounded. */
void fuzz_parsed(const uint8_t * data, size_t size, bool block) {
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
  popts.max_alias_expansion = 1000;
  GTEXT_YAML_Document * doc = gtext_yaml_parse(
      reinterpret_cast<const char *>(data), size, &popts, &err);
  gtext_yaml_error_free(&err);
  if (!doc) return;

  GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
  if (block) {
    wopts.pretty = true;
    wopts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
  }
  std::string text;
  if (write_document(doc, &wopts, &text)) must_round_trip(doc, text);
  gtext_yaml_free(doc);
}

/* The streaming parser straight into the streaming writer, with no DOM in
 * between.  The types fit and the two are not a pipe: the writer takes
 * composed events, and the parser reports structure as it was written - the
 * ":" of a block mapping, the "-" of a block sequence, the "," between two
 * flow entries - leaving composing to its consumer.  So almost everything is
 * refused here, which is the point: the writer used to answer an indicator
 * with OK and write nothing, and a caller who joined the two got no error and
 * a document with its structure gone.
 *
 * What still reaches the writer is a lone scalar with its directives, which
 * is the one shape the two agree on, and that is worth holding to the same
 * property as everything else. */
struct Pipe {
  GTEXT_YAML_Writer * writer;
  GTEXT_YAML_Status status;
};

GTEXT_YAML_Status pipe_event(GTEXT_YAML_Stream * s, const void * ev, void * user) {
  (void)s;
  Pipe * p = static_cast<Pipe *>(user);
  if (p->status != GTEXT_YAML_OK) return p->status;
  p->status = gtext_yaml_writer_event(
      p->writer, static_cast<const GTEXT_YAML_Event *>(ev));
  return p->status;
}

void fuzz_event_pipe(const uint8_t * data, size_t size) {
  /* The event API resolves nothing - an alias to an anchor nobody declared is
     an event like any other - so the stream accepts input the DOM parser
     refuses, and the writer is right to hand back something equally
     unresolvable.  The property only holds where the input was a document:
     "*c%" was the fuzzer's first find, and it was this harness being wrong
     rather than the writer. */
  {
    GTEXT_YAML_Error probe;
    memset(&probe, 0, sizeof(probe));
    GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
    popts.max_alias_expansion = 1000;
    GTEXT_YAML_Document * doc = gtext_yaml_parse(
        reinterpret_cast<const char *>(data), size, &popts, &probe);
    gtext_yaml_error_free(&probe);
    if (!doc) return;
    gtext_yaml_free(doc);
  }

  GTEXT_YAML_Sink sink;
  if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) return;
  GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
  Pipe pipe{gtext_yaml_writer_new(sink, &wopts), GTEXT_YAML_OK};
  if (!pipe.writer) { gtext_yaml_sink_buffer_free(&sink); return; }

  GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
  popts.max_alias_expansion = 1000;
  popts.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;
  GTEXT_YAML_Stream * st = gtext_yaml_stream_new(&popts, pipe_event, &pipe);
  if (!st) {
    gtext_yaml_writer_free(pipe.writer);
    gtext_yaml_sink_buffer_free(&sink);
    return;
  }
  GTEXT_YAML_Status status = gtext_yaml_stream_feed(
      st, reinterpret_cast<const char *>(data), size);
  if (status == GTEXT_YAML_OK) status = gtext_yaml_stream_finish(st);
  if (status == GTEXT_YAML_OK) status = pipe.status;
  if (status == GTEXT_YAML_OK) status = gtext_yaml_writer_finish(pipe.writer);

  if (status == GTEXT_YAML_OK) {
    std::string text(gtext_yaml_sink_buffer_data(&sink),
                     gtext_yaml_sink_buffer_size(&sink));
    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document * back =
        gtext_yaml_parse(text.data(), text.size(), &popts, &err);
    gtext_yaml_error_free(&err);
    if (!back) {
      /* Printed before the trap, for the same reason must_round_trip() prints:
         an artifact is a handful of bytes and says nothing on its own. This
         path used to trap in silence, which made every find here a fresh
         reverse-engineering job. */
      fprintf(stderr, "the event pipe wrote what the parser refuses:\n"
                      "  in    %.*s\n  wrote %.*s\n",
              (int)size, reinterpret_cast<const char *>(data),
              (int)text.size(), text.data());
      __builtin_trap();
    }

    /* And it must be the same document, not merely a readable one. */
    GTEXT_YAML_Document * from = gtext_yaml_parse(
        reinterpret_cast<const char *>(data), size, &popts, &err);
    gtext_yaml_error_free(&err);
    if (from) {
      bool a_ok = false, b_ok = false;
      std::string a = values_of(from, &a_ok);
      std::string b = values_of(back, &b_ok);
      gtext_yaml_free(from);
      if (a_ok && b_ok && a != b) {
        fprintf(stderr, "the event pipe wrote a different document:\n"
                        "  in     %.*s\n  wrote  %.*s\n"
                        "  before %s\n  after  %s\n",
                (int)size, reinterpret_cast<const char *>(data),
                (int)text.size(), text.data(), a.c_str(), b.c_str());
        __builtin_trap();
      }
    }
    gtext_yaml_free(back);
  }

  gtext_yaml_stream_free(st);
  gtext_yaml_writer_free(pipe.writer);
  gtext_yaml_sink_buffer_free(&sink);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) return 0;
  const uint8_t mode = data[0];
  const uint8_t * body = data + 1;
  const size_t len = size - 1;

  switch (mode % 5) {
    case 0: fuzz_built_dom(body, len, false); break;
    case 1: fuzz_built_dom(body, len, true); break;
    case 2: fuzz_parsed(body, len, false); break;
    case 3: fuzz_parsed(body, len, true); break;
    default: fuzz_event_pipe(body, len); break;
  }
  return 0;
}
