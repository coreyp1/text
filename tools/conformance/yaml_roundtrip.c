/* Read a YAML stream on stdin; write it back out with this library's writer,
   parse that, and report whether the two agree.
 *
 * Prints one of:
 *   OK                 the document survived the trip
 *   SKIP <why>         the input was not a document to begin with
 *   FAIL <stage> <why> the writer or the re-parse disagreed
 *
 * The comparison is over the composed event stream with scalar style left
 * out, because the writer deliberately chooses its own styles (a parse-write
 * cycle is semantically faithful, not textually faithful). Everything else -
 * structure, order, anchors, tags, the text of every scalar - has to match.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/yaml.h>

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  int failed;
} Sink;

static void sink_add(Sink *s, const char *text) {
  size_t n = strlen(text);
  if (s->len + n + 1 > s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 4096;
    while (cap < s->len + n + 1) cap *= 2;
    char *next = realloc(s->buf, cap);
    if (!next) { s->failed = 1; return; }
    s->buf = next;
    s->cap = cap;
  }
  memcpy(s->buf + s->len, text, n);
  s->len += n;
  s->buf[s->len] = '\0';
}

static GTEXT_YAML_Status record(const GTEXT_YAML_Node_Event *ev, void *user) {
  Sink *s = (Sink *)user;
  switch (ev->type) {
    case GTEXT_YAML_NODE_EVENT_STREAM_START: sink_add(s, "+STR\n"); break;
    case GTEXT_YAML_NODE_EVENT_STREAM_END:   sink_add(s, "-STR\n"); break;
    case GTEXT_YAML_NODE_EVENT_DOCUMENT_START: sink_add(s, "+DOC\n"); break;
    case GTEXT_YAML_NODE_EVENT_DOCUMENT_END:   sink_add(s, "-DOC\n"); break;
    case GTEXT_YAML_NODE_EVENT_SEQUENCE_END:   sink_add(s, "-SEQ\n"); break;
    case GTEXT_YAML_NODE_EVENT_MAPPING_END:    sink_add(s, "-MAP\n"); break;
    case GTEXT_YAML_NODE_EVENT_SEQUENCE_START:
    case GTEXT_YAML_NODE_EVENT_MAPPING_START:
      sink_add(s, ev->type == GTEXT_YAML_NODE_EVENT_SEQUENCE_START
          ? "+SEQ" : "+MAP");
      if (ev->anchor) { sink_add(s, " &"); sink_add(s, ev->anchor); }
      if (ev->tag) { sink_add(s, " <"); sink_add(s, ev->tag); sink_add(s, ">"); }
      sink_add(s, "\n");
      break;
    case GTEXT_YAML_NODE_EVENT_SCALAR: {
      sink_add(s, "=VAL");
      if (ev->anchor) { sink_add(s, " &"); sink_add(s, ev->anchor); }
      if (ev->tag) { sink_add(s, " <"); sink_add(s, ev->tag); sink_add(s, ">"); }
      sink_add(s, " ");
      char *copy = malloc(ev->value_len + 1);
      if (!copy) { s->failed = 1; return GTEXT_YAML_E_OOM; }
      memcpy(copy, ev->value, ev->value_len);
      copy[ev->value_len] = '\0';
      sink_add(s, copy);
      free(copy);
      sink_add(s, "\n");
      break;
    }
    case GTEXT_YAML_NODE_EVENT_ALIAS:
      sink_add(s, "=ALI *");
      sink_add(s, ev->value ? ev->value : "");
      sink_add(s, "\n");
      break;
  }
  return s->failed ? GTEXT_YAML_E_OOM : GTEXT_YAML_OK;
}

/* Render a whole parsed stream as events, or NULL. */
static char *events_of(GTEXT_YAML_Document **docs, size_t count) {
  Sink s;
  memset(&s, 0, sizeof(s));
  if (gtext_yaml_stream_walk(docs, count, record, &s) != GTEXT_YAML_OK) {
    free(s.buf);
    return NULL;
  }
  return s.buf ? s.buf : strdup("");
}

int main(int argc, char **argv) {
  /* "-w" prints the written YAML and stops, so a caller can compare the two
     sides any way it likes - by value as well as by event. */
  const int write_only = (argc > 1 && strcmp(argv[1], "-w") == 0);
  static char in[1 << 20];
  size_t n = fread(in, 1, sizeof(in) - 1, stdin);
  in[n] = 0;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
  popts.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;

  size_t count = 0;
  GTEXT_YAML_Document **docs = gtext_yaml_parse_all(in, n, &count, &popts, &err);
  if (!docs) {
    printf("SKIP not a document: %s\n", err.message ? err.message : "?");
    return 0;
  }

  char *before = write_only ? strdup("") : events_of(docs, count);
  if (!before) { printf("FAIL walk before\n"); return 0; }

  /* Write every document back out, one stream. */
  Sink text;
  memset(&text, 0, sizeof(text));
  int wrote = 1;
  {
    GTEXT_YAML_Sink sink;
    if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) {
      printf("FAIL sink\n");
      return 0;
    }
    GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
    /* YTS_RT_BLOCK asks for the block style a human would write, so the two
       defaults can be told apart from the writer itself. */
    if (getenv("YTS_RT_BLOCK")) {
      wopts.pretty = true;
      wopts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
    }
    GTEXT_YAML_Status wst = (count == 1)
      ? gtext_yaml_write_document(docs[0], &sink, &wopts)
      : gtext_yaml_write_documents(docs, count, &sink, &wopts);
    if (wst != GTEXT_YAML_OK) {
      printf("FAIL write: status %d\n", (int)wst);
      gtext_yaml_sink_buffer_free(&sink);
      wrote = 0;
    } else {
      const char *data = gtext_yaml_sink_buffer_data(&sink);
      size_t len = gtext_yaml_sink_buffer_size(&sink);
      char *copy = malloc(len + 1);
      if (copy) {
        memcpy(copy, data, len);
        copy[len] = 0;
        sink_add(&text, copy);
        free(copy);
      }
      gtext_yaml_sink_buffer_free(&sink);
    }
  }
  if (!wrote) { free(before); free(text.buf); return 0; }

  if (write_only) {
    fwrite(text.buf ? text.buf : "", 1, text.len, stdout);
    if (text.len && text.buf[text.len - 1] != '\n') putchar('\n');
    return 0;
  }

  memset(&err, 0, sizeof(err));
  size_t count2 = 0;
  GTEXT_YAML_Document **docs2 =
      gtext_yaml_parse_all(text.buf ? text.buf : "", text.len, &count2, &popts, &err);
  if (!docs2) {
    printf("FAIL reparse: %s\n", err.message ? err.message : "?");
    printf("---- written ----\n%s----\n", text.buf ? text.buf : "");
    return 0;
  }

  char *after = events_of(docs2, count2);
  if (!after) { printf("FAIL walk after\n"); return 0; }

  if (strcmp(before, after) != 0) {
    printf("FAIL differs\n");
    printf("---- written ----\n%s", text.buf ? text.buf : "");
    printf("---- before ----\n%s---- after ----\n%s", before, after);
  } else {
    printf("OK\n");
  }

  free(before); free(after); free(text.buf);
  for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
  free(docs);
  for (size_t i = 0; i < count2; i++) gtext_yaml_free(docs2[i]);
  free(docs2);
  return 0;
}
