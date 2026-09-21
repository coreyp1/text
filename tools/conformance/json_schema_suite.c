/* Run JSON-Schema-Test-Suite files through this library's schema engine.
 *
 *     json_schema_suite <file.json> [<file.json> ...]
 *
 * One TSV record per line on stdout, for json_schema_suite.py to score:
 *
 *     PASS <file> <group> <test>
 *     FAIL <file> <group> <test> <expected> <actual> <detail>
 *     SKIP <file> <group> <n> <keyword>
 *     BAD  <file> <group> <n> <detail>
 *
 * SKIP and BAD carry the number of assertions the group holds, which is the
 * point of this driver rather than the one in `regex`. A refused schema takes
 * its whole group out of the run, and a score that quietly drops those
 * assertions from the denominator reports a number that only goes up as the
 * engine refuses more. The suite's denominator is fixed; ours must be too.
 *
 * `pattern` and `patternProperties` need a regular-expression provider, which
 * this library deliberately does not contain. When ghoti.io-regex is
 * installed the runner is built with GTEXT_CONFORMANCE_HAVE_REGEX and plugs it
 * in; without it those groups are refused like any other unsupported keyword,
 * and the scorer says which of the two runs it is looking at.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GTEXT_CONFORMANCE_HAVE_REGEX
#include <ghoti.io/regex/regex.h>

typedef struct {
  GRX_Limits limits;
} SchemaRegex;

static int schema_regex_compile(void * ctx, const char * pattern,
    size_t pattern_len, void ** out_regex, char * message,
    size_t message_capacity, size_t * out_offset) {
  SchemaRegex * self = (SchemaRegex *)ctx;
  GRX_Error error;
  GRX_Regex * regex = NULL;
  grx_error_clear(&error);
  /* ECMA-262 with the `u` flag, which is what JSON Schema core section 6.4
   * names. Any other dialect reads `\d` differently. */
  if (grx_regex_compile_with_allocator(pattern, pattern_len,
          GRX_SYNTAX_ECMASCRIPT, GRX_OPT_UTF, &self->limits, NULL, &error,
          &regex)
      != GRX_OK) {
    snprintf(message, message_capacity, "%s", error.message);
    *out_offset = error.offset;
    return 1;
  }
  *out_regex = regex;
  return 0;
}

static int schema_regex_search(
    void * ctx, void * regex, const char * subject, size_t subject_len) {
  SchemaRegex * self = (SchemaRegex *)ctx;
  int matched = 0;
  if (grx_regex_search(regex, subject, subject_len, 0, GRX_ENGINE_AUTO,
          &self->limits, NULL, &matched)
      != GRX_OK) {
    return -1;
  }
  return matched ? 1 : 0;
}

static void schema_regex_free(void * ctx, void * regex) {
  (void)ctx;
  grx_regex_free((GRX_Regex *)regex);
}
#endif

/* Printed into a TSV field, so a tab or a newline in a description would
 * shift every column after it. The suite has none today; a corpus that grows
 * one should not silently corrupt the score. */
static void put_field(const char * text) {
  if (!text) {
    fputs("?", stdout);
    return;
  }
  for (const unsigned char * p = (const unsigned char *)text; *p; p++) {
    putchar((*p < 0x20) ? ' ' : *p);
  }
}

static char * read_file(const char * path, size_t * out_len) {
  FILE * file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  size_t capacity = 65536;
  size_t length = 0;
  char * data = (char *)malloc(capacity);
  if (!data) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (length == capacity) {
      char * grown = (char *)realloc(data, capacity * 2);
      if (!grown) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
      capacity *= 2;
    }
    size_t got = fread(data + length, 1, capacity - length, file);
    length += got;
    if (got == 0) {
      break;
    }
  }
  fclose(file);
  *out_len = length;
  return data;
}

static const char * description_of(const GTEXT_JSON_Value * value) {
  const GTEXT_JSON_Value * field =
      gtext_json_object_get(value, "description", 11);
  const char * text = NULL;
  size_t length = 0;
  if (!field || gtext_json_get_string(field, &text, &length) != GTEXT_JSON_OK) {
    return "(no description)";
  }
  return text;
}

static void run_group(const char * file, const GTEXT_JSON_Value * group,
    const GTEXT_JSON_Regex_Provider * provider) {
  const GTEXT_JSON_Value * schema_doc =
      gtext_json_object_get(group, "schema", 6);
  const GTEXT_JSON_Value * tests = gtext_json_object_get(group, "tests", 5);
  if (!schema_doc || !tests || gtext_json_typeof(tests) != GTEXT_JSON_ARRAY) {
    return;
  }
  size_t count = gtext_json_array_size(tests);

  GTEXT_JSON_Schema_Options options = gtext_json_schema_options_default();
  options.regex = provider;
  GTEXT_JSON_Error error;
  memset(&error, 0, sizeof(error));
  GTEXT_JSON_Schema * schema =
      gtext_json_schema_compile_with_options(schema_doc, &options, &error);
  if (!schema) {
    /* A keyword this engine does not implement is a skip; anything else is a
     * wrong answer, because every schema in the suite is a valid schema. */
    int unsupported = (error.code == GTEXT_JSON_E_SCHEMA_UNSUPPORTED);
    printf("%s\t", unsupported ? "SKIP" : "BAD");
    put_field(file);
    putchar('\t');
    put_field(description_of(group));
    printf("\t%zu\t", count);
    /* The keyword travels in context_snippet; a refusal that names none -
     * the $ref family does not - falls back to the message, so that the
     * report never has a bucket called "?". */
    if (unsupported && error.context_snippet) {
      put_field(error.context_snippet);
    }
    else {
      put_field(error.message ? error.message : "?");
    }
    putchar('\n');
    gtext_json_error_free(&error);
    return;
  }

  for (size_t i = 0; i < count; i++) {
    const GTEXT_JSON_Value * test = gtext_json_array_get(tests, i);
    const GTEXT_JSON_Value * data = gtext_json_object_get(test, "data", 4);
    const GTEXT_JSON_Value * valid = gtext_json_object_get(test, "valid", 5);
    bool expected = false;
    if (!data || !valid
        || gtext_json_get_bool(valid, &expected) != GTEXT_JSON_OK) {
      continue;
    }

    memset(&error, 0, sizeof(error));
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, data, &error);
    /* Three answers, not two: GTEXT_JSON_E_LIMIT says the engine did not
     * reach one, and reporting that as "invalid" would turn a budget into a
     * validation result. */
    const char * actual = (status == GTEXT_JSON_OK)      ? "valid"
                          : (status == GTEXT_JSON_E_SCHEMA) ? "invalid"
                                                            : "undecided";
    const char * want = expected ? "valid" : "invalid";
    if (strcmp(actual, want) == 0) {
      fputs("PASS\t", stdout);
      put_field(file);
      putchar('\t');
      put_field(description_of(group));
      putchar('\t');
      put_field(description_of(test));
      putchar('\n');
    }
    else {
      fputs("FAIL\t", stdout);
      put_field(file);
      putchar('\t');
      put_field(description_of(group));
      putchar('\t');
      put_field(description_of(test));
      printf("\t%s\t%s\t", want, actual);
      put_field(error.message ? error.message : "");
      putchar('\n');
    }
    gtext_json_error_free(&error);
  }

  gtext_json_schema_free(schema);
}

int main(int argc, char ** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: json_schema_suite <file.json> [<file.json> ...]\n");
    return 2;
  }

  const GTEXT_JSON_Regex_Provider * provider = NULL;
#ifdef GTEXT_CONFORMANCE_HAVE_REGEX
  SchemaRegex self;
  GTEXT_JSON_Regex_Provider grx;
  grx_limits_default(&self.limits);
  grx.ctx = &self;
  grx.compile_fn = schema_regex_compile;
  grx.search_fn = schema_regex_search;
  grx.free_fn = schema_regex_free;
  provider = &grx;
#endif

  GTEXT_JSON_Parse_Options parse_options = gtext_json_parse_options_default();

  for (int i = 1; i < argc; i++) {
    size_t length = 0;
    char * text = read_file(argv[i], &length);
    if (!text) {
      fprintf(stderr, "cannot read %s\n", argv[i]);
      return 2;
    }
    GTEXT_JSON_Error error;
    memset(&error, 0, sizeof(error));
    GTEXT_JSON_Value * doc =
        gtext_json_parse(text, length, &parse_options, &error);
    if (!doc || gtext_json_typeof(doc) != GTEXT_JSON_ARRAY) {
      fprintf(stderr, "%s is not a JSON-Schema-Test-Suite file\n", argv[i]);
      gtext_json_free(doc);
      free(text);
      return 2;
    }

    /* The basename alone, so a record does not carry whatever directory the
     * suite happens to be checked out into. */
    const char * name = strrchr(argv[i], '/');
    name = name ? name + 1 : argv[i];

    size_t groups = gtext_json_array_size(doc);
    for (size_t g = 0; g < groups; g++) {
      run_group(name, gtext_json_array_get(doc, g), provider);
    }

    gtext_json_free(doc);
    free(text);
  }
  return 0;
}
