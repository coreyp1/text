/* Read a YAML stream on stdin; print one JSON document per line, or FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>

int main(void) {
  static char buf[1 << 20];
  size_t n = fread(buf, 1, sizeof(buf) - 1, stdin);
  buf[n] = 0;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  size_t count = 0;
  GTEXT_YAML_Document **docs = gtext_yaml_parse_all(buf, n, &count, NULL, &err);
  if (!docs) {
    printf("FAIL parse: %s\n", err.message ? err.message : "?");
    return 0;
  }

  for (size_t i = 0; i < count; i++) {
    GTEXT_JSON_Value *jv = NULL;
    memset(&err, 0, sizeof(err));
    /* The reference implementations resolve aliases and merge keys and
       coerce non-string keys, so the comparison uses the same settings
       rather than this library's stricter defaults. */
    GTEXT_YAML_To_JSON_Options opts = gtext_yaml_to_json_options_default();
    opts.allow_resolved_aliases = true;
    opts.allow_merge_keys = true;
    opts.coerce_keys_to_strings = true;
    GTEXT_YAML_Status st = gtext_yaml_to_json_with_options(docs[i], &jv, &opts, &err);
    if (st != GTEXT_YAML_OK) {
      printf("FAIL tojson: %s\n", err.message ? err.message : "?");
      continue;
    }
    if (!jv) { printf("null\n"); continue; }
    GTEXT_JSON_Sink sink;
    if (gtext_json_sink_buffer(&sink) != GTEXT_JSON_OK) {
      printf("FAIL sink\n");
      gtext_json_free(jv);
      continue;
    }
    GTEXT_JSON_Error jerr;
    memset(&jerr, 0, sizeof(jerr));
    if (gtext_json_write_value(&sink, NULL, jv, &jerr) != GTEXT_JSON_OK) {
      printf("FAIL write: %s\n", jerr.message ? jerr.message : "?");
    } else {
      const char *data = gtext_json_sink_buffer_data(&sink);
      size_t len = gtext_json_sink_buffer_size(&sink);
      fwrite(data, 1, len, stdout);
      putchar('\n');
    }
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(jv);
  }
  for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
  free(docs);
  return 0;
}
