// Internal utility functions for JSON module

#include "json_internal.h"
#include <string.h>

// Check if a length-delimited string exactly equals a null-terminated keyword
int json_matches(const char* input, size_t len, const char* keyword) {
    size_t keyword_len = strlen(keyword);
    return len == keyword_len && memcmp(input, keyword, keyword_len) == 0;
}
