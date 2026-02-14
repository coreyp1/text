@page example_yaml_writer_formatting yaml_writer_formatting.c - Writer Formatting Options

# yaml_writer_formatting.c - Writer Formatting Options

This example shows how to control YAML formatting when emitting a document from the DOM.

## What This Example Demonstrates

- Pretty (block) output for collections
- Custom indentation width
- Scalar style selection (folded scalars)
- Line-width aware folding

## Conceptual Source Code

```c
#include <ghoti.io/text/yaml.h>
#include <stdio.h>

int main(void) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "notes", NULL, NULL);
  GTEXT_YAML_Node *value = gtext_yaml_node_new_scalar(
      doc, "one two three four", NULL, NULL);
  map = gtext_yaml_mapping_set(doc, map, key, value);
  gtext_yaml_document_set_root(doc, map);

  GTEXT_YAML_Sink sink;
  gtext_yaml_sink_buffer(&sink);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.indent_spaces = 4;
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
  opts.line_width = 10;

  gtext_yaml_write_document(doc, &sink, &opts);
  printf("%s", gtext_yaml_sink_buffer_data(&sink));

  gtext_yaml_sink_buffer_free(&sink);
  gtext_yaml_free(doc);
  return 0;
}
```

## Expected Output

```yaml
notes: >
    one two
    three
    four
```

## Key API Functions Used

- `gtext_yaml_write_document()` - Serialize the DOM to a sink
- `gtext_yaml_write_options_default()` - Create formatting options
- `gtext_yaml_sink_buffer()` - Capture output in memory

## Related Examples

- [yaml_streaming_basic.c](@ref example_yaml_streaming_basic) - Streaming parser basics
- [yaml_config_parser.c](@ref example_yaml_config_parser) - Parse config files
