/**
 * @file
 *
 * The JSON fast path against the general parser, over yaml-test-suite.
 *
 * gtext_yaml_parse() has two implementations.  Input that is also JSON is
 * handed to the JSON parser and converted, which is faster and is on by
 * default; everything else goes through the YAML scanner.  Two
 * implementations of one contract drift, and this one had: a JSON string
 * became a scalar with no style, and the resolver resolves a scalar by its
 * contents only when it was written plain - so ["0x10"] came back as [16],
 * [""] as [null], and the key of {"": ""} as the string "null".
 *
 * Nothing caught it, and the reason is worth keeping in view: the conformance
 * runner asks for GTEXT_YAML_DUPKEY_KEEP_ALL, because the suite has duplicate
 * keys it must not collapse - and that is the one setting which turns the
 * fast path off.  All 395 cases had only ever gone the other way.
 *
 * This asks no expectation of either side.  It asks only that they agree,
 * which needs no corpus of answers and covers every document the suite has.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char * as_json(const char * src, size_t n, int fast, int * refused) {
  GTEXT_YAML_Error e; memset(&e, 0, sizeof e);
  GTEXT_YAML_Parse_Options p = gtext_yaml_parse_options_default();
  p.enable_json_fast_path = fast ? true : false;
  GTEXT_YAML_Document * d = gtext_yaml_parse(src, n, &p, &e);
  if (!d) { *refused = 1; gtext_yaml_error_free(&e); return NULL; }
  *refused = 0;
  GTEXT_JSON_Value * j = NULL;
  GTEXT_YAML_To_JSON_Options jo = gtext_yaml_to_json_options_default();
  jo.allow_resolved_aliases = true; jo.allow_merge_keys = true;
  jo.coerce_keys_to_strings = true;
  char * out = NULL;
  if (gtext_yaml_to_json_with_options(d, &j, &jo, &e) == GTEXT_YAML_OK && j) {
    GTEXT_JSON_Sink s; GTEXT_JSON_Error je; memset(&je, 0, sizeof je);
    if (gtext_json_sink_buffer(&s) == GTEXT_JSON_OK) {
      if (gtext_json_write_value(&s, NULL, j, &je) == GTEXT_JSON_OK) {
        size_t len = gtext_json_sink_buffer_size(&s);
        out = malloc(len + 1);
        memcpy(out, gtext_json_sink_buffer_data(&s), len);
        out[len] = 0;
      }
      gtext_json_sink_buffer_free(&s);
    }
    gtext_json_free(j);
  }
  if (!out) out = strdup("(no json)");
  gtext_yaml_free(d); gtext_yaml_error_free(&e);
  return out;
}

int main(void) {
  static char in[1 << 20];
  size_t n = fread(in, 1, sizeof in - 1, stdin);
  in[n] = 0;
  int rf = 0, rs = 0;
  /* Whether the fast path is taken at all.  An agreement figure over
     documents that never reach it says nothing, and most of a YAML corpus
     never does - the two paths agree on those the way any function agrees
     with itself. */
  {
    /* The library's own candidate test is internal, and this is close enough
       for a denominator: the fast path is taken only for input the JSON
       parser accepts whole. */
    GTEXT_JSON_Error je;
    memset(&je, 0, sizeof je);
    GTEXT_JSON_Value * probe = gtext_json_parse(in, n, NULL, &je);
    gtext_json_error_free(&je);
    if (!probe) {
      printf("OK not-json\n");
      return 0;
    }
    gtext_json_free(probe);
  }
  char * fast = as_json(in, n, 1, &rf);
  char * slow = as_json(in, n, 0, &rs);
  if (rf != rs) {
    printf("DIFFER accept: fast %s, slow %s\n",
           rf ? "refused" : "accepted", rs ? "refused" : "accepted");
  } else if (!rf && strcmp(fast, slow) != 0) {
    printf("DIFFER value:\n  fast %s\n  slow %s\n", fast, slow);
  } else {
    printf("OK\n");
  }
  free(fast); free(slow);
  return 0;
}
