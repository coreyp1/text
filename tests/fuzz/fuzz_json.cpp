/**
 * @file
 *
 * libFuzzer harness for the JSON parser.
 *
 * The contract being fuzzed: for any byte string at all, the parser either
 * returns a document or reports an error. It must not crash, read out of
 * bounds, or leak - and if it does return a document, walking that document
 * must be safe.
 *
 * The parse options are drawn from the first input byte so that the relaxed
 * extensions (comments, trailing commas, single quotes, non-finite numbers)
 * are reached as well as strict mode, which is what a fixed-options harness
 * would miss.
 *
 * Build with: make fuzz-json     Run: make fuzz-run-json
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>

extern "C" {
#include <ghoti.io/text/json.h>
}

/** Walk a value so the accessors are exercised, not just the parser. */
static void walk(const GTEXT_JSON_Value * v, int depth) {
  if (!v || depth > 300) {
    return;
  }
  switch (gtext_json_typeof(v)) {
    case GTEXT_JSON_ARRAY: {
      size_t n = gtext_json_array_size(v);
      for (size_t i = 0; i < n; i++) {
        walk(gtext_json_array_get(v, i), depth + 1);
      }
      break;
    }
    case GTEXT_JSON_OBJECT: {
      size_t n = gtext_json_object_size(v);
      for (size_t i = 0; i < n; i++) {
        size_t key_len = 0;
        (void)gtext_json_object_key(v, i, &key_len);
        walk(gtext_json_object_value(v, i), depth + 1);
      }
      break;
    }
    case GTEXT_JSON_STRING: {
      const char * s = nullptr;
      size_t len = 0;
      (void)gtext_json_get_string(v, &s, &len);
      break;
    }
    case GTEXT_JSON_NUMBER: {
      double d = 0.0;
      int64_t i = 0;
      (void)gtext_json_get_double(v, &d);
      (void)gtext_json_get_i64(v, &i);
      break;
    }
    default:
      break;
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  // First byte selects the dialect; the rest is the document.
  const uint8_t flags = data[0];
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t len = size - 1;

  GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
  opts.allow_comments = (flags & 0x01) != 0;
  opts.allow_trailing_commas = (flags & 0x02) != 0;
  opts.allow_nonfinite_numbers = (flags & 0x04) != 0;
  opts.allow_single_quotes = (flags & 0x08) != 0;
  opts.allow_unescaped_controls = (flags & 0x10) != 0;
  opts.parse_int64 = (flags & 0x20) != 0;
  opts.parse_double = (flags & 0x40) != 0;
  opts.preserve_number_lexeme = (flags & 0x80) != 0;

  // Value-initialized: the parser frees any snippet already in the struct
  // before writing a new one, so an indeterminate pointer here would be
  // passed to free(). And the snippet it allocates belongs to the caller.
  GTEXT_JSON_Error err{};
  GTEXT_JSON_Value * root = gtext_json_parse(text, len, &opts, &err);
  if (root) {
    walk(root, 0);
    gtext_json_free(root);
  }
  gtext_json_error_free(&err);
  return 0;
}
