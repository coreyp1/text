#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ghoti.io/text/yaml/yaml_core.h>
#include "yaml_internal.h"

int main(void) {
    const char *input = "foo - bar";
    GTEXT_YAML_Scanner *s = gtext_yaml_scanner_new();
    if (!s) { fprintf(stderr, "scanner new failed\n"); return 1; }
    if (!gtext_yaml_scanner_feed(s, input, strlen(input))) { fprintf(stderr, "feed fail\n"); return 1; }
    GTEXT_YAML_Token tok;
    GTEXT_YAML_Error err;
    for (;;) {
        GTEXT_YAML_Status st = gtext_yaml_scanner_next(s, &tok, &err);
        if (st == GTEXT_YAML_E_INCOMPLETE) { printf("INCOMPLETE\n"); break; }
        if (st != GTEXT_YAML_OK) { printf("STATUS %d\n", st); break; }
        if (tok.type == GTEXT_YAML_TOKEN_EOF) { printf("EOF\n"); break; }
        if (tok.type == GTEXT_YAML_TOKEN_INDICATOR) {
            printf("INDICATOR '%c' at %zu\n", tok.u.c, tok.offset);
        } else if (tok.type == GTEXT_YAML_TOKEN_SCALAR) {
            printf("SCALAR len=%zu data='%.*s' at %zu\n", tok.u.scalar.len, (int)tok.u.scalar.len, tok.u.scalar.ptr, tok.offset);
            free((void *)tok.u.scalar.ptr);
        }
    }
    gtext_yaml_scanner_free(s);
    return 0;
}
