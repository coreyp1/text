/* Read one JSON document on stdin; print ACCEPT, or REJECT and the reason.
 *
 * JSONTestSuite asks only whether a parser takes an input or refuses it, so
 * this says nothing about the value. The duplicate-key policy is a parse
 * option rather than a grammar rule, and the scorer wants to see both
 * settings, so JTS_DUPKEY_LAST_WINS picks the permissive one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/json.h>

int main(void) {
  static char buf[1 << 22];
  size_t n = fread(buf, 1, sizeof(buf), stdin);

  GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
  if (getenv("JTS_DUPKEY_LAST_WINS")) {
    opts.dupkeys = GTEXT_JSON_DUPKEY_LAST_WINS;
  }

  GTEXT_JSON_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_JSON_Value *v = gtext_json_parse(buf, n, &opts, &err);
  if (!v) {
    printf("REJECT %s\n", err.message ? err.message : "?");
    return 0;
  }
  printf("ACCEPT\n");
  gtext_json_free(v);
  return 0;
}
