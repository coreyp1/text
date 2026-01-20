/**
 * @file csv_error.c
 * @brief Error handling utilities for CSV module
 */

#include "csv_internal.h"
#include <ghoti.io/text/csv/csv_core.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

TEXT_API void text_csv_error_free(text_csv_error* err) {
    if (err && err->context_snippet) {
        free(err->context_snippet);
        err->context_snippet = NULL;
        err->context_snippet_len = 0;
        err->caret_offset = 0;
    }
}
