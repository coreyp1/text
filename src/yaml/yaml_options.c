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
#include "yaml_internal.h"

GTEXT_API GTEXT_YAML_Parse_Options gtext_yaml_parse_options_default(void)
{
  GTEXT_YAML_Parse_Options opts = {0};
  opts.dupkeys = GTEXT_YAML_DUPKEY_ERROR;
  opts.schema = GTEXT_YAML_SCHEMA_CORE;
  /* Defaults: 0 means use library default; here we supply concrete defaults */
  opts.max_depth = 256; /* default max nesting */
  opts.max_total_bytes = 64 * 1024 * 1024; /* 64 MiB */
  opts.max_alias_expansion = 10000; /* total alias-expanded nodes limit */
  opts.validate_utf8 = true;
  opts.resolve_tags = true;
  opts.retain_comments = false;
  opts.yaml_1_1 = false;
  opts.enable_custom_tags = false;
  opts.custom_tags = NULL;
  opts.custom_tag_count = 0;
  return opts;
}

GTEXT_API GTEXT_YAML_Write_Options gtext_yaml_write_options_default(void)
{
  GTEXT_YAML_Write_Options opts = {0};
  opts.pretty = false;
  opts.indent_spaces = 2;
  opts.line_width = 0;
  opts.newline = "\n";
  opts.trailing_newline = false;
  opts.canonical = false;
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_PLAIN;
  opts.flow_style = GTEXT_YAML_FLOW_STYLE_AUTO;
  opts.encoding = GTEXT_YAML_ENCODING_UTF8;
  opts.emit_bom = false;
  opts.enable_custom_tags = false;
  opts.custom_tags = NULL;
  opts.custom_tag_count = 0;
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
  if (!doc) return;
  
  /* Free context (which frees arena, resolver, and all nodes) */
  yaml_context_free(doc->ctx);
  
  /* Note: doc itself was allocated from the arena, so it's freed by
   * yaml_context_free(). We don't need to free it separately. */
}
