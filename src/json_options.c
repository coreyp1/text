/**
 * @file json_options.c
 * @brief Default initialization for JSON parse and write options
 */

#include <text/json.h>

text_json_parse_options text_json_parse_options_default(void) {
    text_json_parse_options opts = {0};

    // Strictness / extensions - all off by default (strict JSON)
    opts.allow_comments = 0;
    opts.allow_trailing_commas = 0;
    opts.allow_nonfinite_numbers = 0;
    opts.allow_single_quotes = 0;
    opts.allow_unescaped_controls = 0;

    // Unicode / input handling
    opts.allow_leading_bom = 1;  // default on
    opts.validate_utf8 = 1;      // default on
    opts.normalize_unicode = 0;  // v2 feature, off by default

    // Duplicate keys
    opts.dupkeys = TEXT_JSON_DUPKEY_ERROR;  // fail on duplicate keys

    // Limits - 0 means library default
    opts.max_depth = 0;
    opts.max_string_bytes = 0;
    opts.max_container_elems = 0;
    opts.max_total_bytes = 0;

    // Number fidelity / representations
    opts.preserve_number_lexeme = 1;   // preserve for round-trip correctness
    opts.parse_int64 = 1;              // detect int64
    opts.parse_uint64 = 1;             // detect uint64
    opts.parse_double = 1;             // derive double
    opts.allow_big_decimal = 0;        // off by default

    return opts;
}

text_json_write_options text_json_write_options_default(void) {
    text_json_write_options opts = {0};

    // Formatting
    opts.pretty = 0;              // compact output
    opts.indent_spaces = 2;       // default indent (used if pretty = 1)
    opts.newline = "\n";          // default newline

    // Escaping
    opts.escape_solidus = 0;       // don't escape forward slash by default
    opts.escape_unicode = 0;       // don't escape non-ASCII by default
    opts.escape_all_non_ascii = 0; // don't escape all non-ASCII

    // Canonical / deterministic
    opts.sort_object_keys = 0;     // preserve insertion order
    opts.canonical_numbers = 0;    // preserve original lexeme
    opts.canonical_strings = 0;    // preserve original escapes

    // Extensions
    opts.allow_nonfinite_numbers = 0;  // don't emit nonfinite by default

    return opts;
}
