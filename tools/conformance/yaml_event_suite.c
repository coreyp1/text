/* Read a YAML stream on stdin; print yaml-test-suite's event notation, or
   FAIL if the stream is refused.
 
   The notation is the suite's, not this library's: "+STR", "+DOC ---",
   "+MAP {}", "=VAL &a <tag:yaml.org,2002:str> \"text", "=ALI *a".  The events
   come from gtext_yaml_stream_walk(); everything below is spelling.

   Two conversions are the suite's conventions rather than YAML's:

   - A tag is printed as a URI in angle brackets, so the "!!" shorthand is
     expanded here.  "!!str" and "tag:yaml.org,2002:str" are the same tag and
     the library reports the node's own spelling; which one to print is the
     harness's question.
   - A scalar is printed on one line, so a newline inside one is written "\n".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/yaml.h>

static void print_escaped(const char *s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
    case '\\': fputs("\\\\", stdout); break;
    case '\n': fputs("\\n", stdout); break;
    case '\t': fputs("\\t", stdout); break;
    case '\r': fputs("\\r", stdout); break;
    case '\b': fputs("\\b", stdout); break;
    default: putchar(c); break;
    }
  }
}

/* The suite writes every tag as a URI.  A "!!x" shorthand is the default
   secondary handle, which expands to the tag:yaml.org,2002: prefix; every
   other spelling reaching here is already a URI or a local "!x" tag, both of
   which the suite prints unchanged. */
static void print_tag(const char *tag) {
  if (!tag) return;
  fputs(" <", stdout);
  if (tag[0] == '!' && tag[1] == '!') printf("tag:yaml.org,2002:%s", tag + 2);
  else fputs(tag, stdout);
  putchar('>');
}

static void print_anchor(const char *anchor) {
  if (anchor) printf(" &%s", anchor);
}

static char style_char(GTEXT_YAML_Scalar_Style style) {
  switch (style) {
  case GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED: return '\'';
  case GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED: return '"';
  case GTEXT_YAML_SCALAR_STYLE_LITERAL: return '|';
  case GTEXT_YAML_SCALAR_STYLE_FOLDED: return '>';
  case GTEXT_YAML_SCALAR_STYLE_PLAIN: return ':';
  }
  return ':';
}

static GTEXT_YAML_Status on_event(const GTEXT_YAML_Node_Event *ev, void *user) {
  (void)user;
  switch (ev->type) {
  case GTEXT_YAML_NODE_EVENT_STREAM_START: fputs("+STR\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_STREAM_END:   fputs("-STR\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_DOCUMENT_START:
    fputs(ev->explicit_marker ? "+DOC ---\n" : "+DOC\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_DOCUMENT_END:
    fputs(ev->explicit_marker ? "-DOC ...\n" : "-DOC\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_SEQUENCE_START:
    fputs("+SEQ", stdout);
    if (ev->flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) fputs(" []", stdout);
    print_anchor(ev->anchor); print_tag(ev->tag); putchar('\n');
    break;
  case GTEXT_YAML_NODE_EVENT_SEQUENCE_END: fputs("-SEQ\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_MAPPING_START:
    fputs("+MAP", stdout);
    if (ev->flow_style == GTEXT_YAML_FLOW_STYLE_FLOW) fputs(" {}", stdout);
    print_anchor(ev->anchor); print_tag(ev->tag); putchar('\n');
    break;
  case GTEXT_YAML_NODE_EVENT_MAPPING_END: fputs("-MAP\n", stdout); break;
  case GTEXT_YAML_NODE_EVENT_SCALAR:
    fputs("=VAL", stdout);
    print_anchor(ev->anchor); print_tag(ev->tag);
    putchar(' '); putchar(style_char(ev->scalar_style));
    print_escaped(ev->value, ev->value_len);
    putchar('\n');
    break;
  case GTEXT_YAML_NODE_EVENT_ALIAS:
    fputs("=ALI *", stdout);
    print_escaped(ev->value, ev->value_len);
    putchar('\n');
    break;
  }
  return GTEXT_YAML_OK;
}

int main(void) {
  static char buf[1 << 20];
  size_t n = fread(buf, 1, sizeof(buf) - 1, stdin);
  buf[n] = 0;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  size_t count = 0;
  /* The suite's event streams are what a parser produces, and a parser has no
     opinion about key uniqueness - that is a rule about the mapping the
     events describe (3.2.1.3).  Several cases carry an event stream and no
     JSON precisely because the value would have duplicate keys, so scoring
     them means keeping the pairs the document holds. */
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;
  GTEXT_YAML_Document **docs = gtext_yaml_parse_all(buf, n, &count, &opts, &err);
  if (!docs) {
    printf("FAIL parse: %s\n", err.message ? err.message : "?");
    return 0;
  }

  GTEXT_YAML_Status st = gtext_yaml_stream_walk(docs, count, on_event, NULL);
  if (st != GTEXT_YAML_OK) printf("FAIL walk: %d\n", (int)st);

  for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
  free(docs);
  return 0;
}
