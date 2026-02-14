/**
 * @file
 *
 * Example: YAML writer formatting options.
 *
 * Demonstrates pretty output, custom indentation, and scalar styles.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
  if (!doc) {
    fprintf(stderr, "Failed to create document.\n");
    return 1;
  }

  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "notes", NULL, NULL);
  GTEXT_YAML_Node *value = gtext_yaml_node_new_scalar(
      doc, "one two three four", NULL, NULL);
  map = gtext_yaml_mapping_set(doc, map, key, value);
  if (!map || !gtext_yaml_document_set_root(doc, map)) {
    fprintf(stderr, "Failed to build document.\n");
    gtext_yaml_free(doc);
    return 1;
  }

  GTEXT_YAML_Sink sink;
  if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) {
    fprintf(stderr, "Failed to create sink.\n");
    gtext_yaml_free(doc);
    return 1;
  }

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.indent_spaces = 4;
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
  opts.line_width = 10;

  if (gtext_yaml_write_document(doc, &sink, &opts) != GTEXT_YAML_OK) {
    fprintf(stderr, "Failed to write YAML.\n");
    gtext_yaml_sink_buffer_free(&sink);
    gtext_yaml_free(doc);
    return 1;
  }

  const char *output = gtext_yaml_sink_buffer_data(&sink);
  printf("%s", output ? output : "");

  gtext_yaml_sink_buffer_free(&sink);
  gtext_yaml_free(doc);
  return 0;
}
