/**
 * @file
 * @brief Default options for the YAML module.
 *
 * Minimal stub implementation used to wire defaults into the test harness.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdlib.h>

#include <ghoti.io/text/yaml/yaml_core.h>

GTEXT_API GTEXT_YAML_Parse_Options gtext_yaml_parse_options_default(void)
{
  GTEXT_YAML_Parse_Options opts = {0};
  opts.dupkeys = GTEXT_YAML_DUPKEY_ERROR;
  /* Defaults: 0 means use library default; here we supply concrete defaults */
  opts.max_depth = 256; /* default max nesting */
  opts.max_total_bytes = 64 * 1024 * 1024; /* 64 MiB */
  opts.max_alias_expansion = 10000; /* total alias-expanded nodes limit */
  opts.validate_utf8 = true;
  opts.resolve_tags = true;
  opts.retain_comments = false;
  return opts;
}

GTEXT_API GTEXT_YAML_Write_Options gtext_yaml_write_options_default(void)
{
  GTEXT_YAML_Write_Options opts = {0};
  opts.pretty = false;
  opts.indent_spaces = 2;
  opts.newline = "\n";
  opts.trailing_newline = false;
  opts.canonical = false;
  return opts;
}

GTEXT_API void gtext_yaml_error_free(GTEXT_YAML_Error *err)
{
  if (!err) {
    return;
  }

  if (err->context_snippet) {
    free(err->context_snippet);
    err->context_snippet = NULL;
    err->context_snippet_len = 0;
  }
}

GTEXT_API void gtext_yaml_free(GTEXT_YAML_Document *doc)
{
  /* Placeholder: real implementation will free arena and nodes. */
  (void)doc;
}
