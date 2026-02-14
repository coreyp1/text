@page example_yaml_streaming_basic yaml_streaming_basic.c - Basic Streaming YAML Parser

# yaml_streaming_basic.c - Basic Streaming YAML Parser

This example demonstrates the fundamental streaming YAML parser: parsing YAML documents using the event-driven callback interface.

## What This Example Demonstrates

- **Creating a streaming parser** - Initialize parser with options
- **Feeding input in chunks** - Handle arbitrary chunk boundaries
- **Event callback handling** - Process parser events (scalars, indicators, etc.)
- **Finishing and cleanup** - Complete parsing and free resources
- **Error handling** - Proper error checking and reporting

## Use Case

This is the ideal starting point for YAML parsing. The streaming API is:
- **Memory efficient** - No full DOM in memory
- **Flexible** - Handle large files or network streams
- **Fast** - Process events as they arrive

Use this when:
- Parsing configuration files
- Processing large YAML documents
- Building custom data structures from YAML
- Need low memory footprint

## Example Output

For the input YAML:
```yaml
name: My Application
version: 1.0.0
database:
  host: localhost
  port: 5432
```

The parser emits events like:
- `SCALAR: "name"`
- `INDICATOR: ':'`
- `SCALAR: "My Application"`
- `SCALAR: "database"`
- `INDICATOR: ':'`
- `SCALAR: "host"`
- etc.

## Conceptual Source Code

```c
#include <ghoti.io/text/yaml.h>
#include <stdio.h>
#include <string.h>

// Event callback: called for each parser event
GTEXT_YAML_Status event_callback(
    GTEXT_YAML_Stream *s,
    const void *event_payload,
    void *user
) {
    const GTEXT_YAML_Event *ev = (const GTEXT_YAML_Event *)event_payload;
    
    switch (ev->type) {
        case GTEXT_YAML_EVENT_STREAM_START:
            printf("Stream started\n");
            break;
            
        case GTEXT_YAML_EVENT_DOCUMENT_START:
            printf("Document started\n");
            break;
            
        case GTEXT_YAML_EVENT_SCALAR:
            printf("Scalar: '%.*s' (at offset %zu, line %d, col %d)\n",
                   (int)ev->data.scalar.len,
                   ev->data.scalar.ptr,
                   ev->offset,
                   ev->line,
                   ev->col);
            break;
            
        case GTEXT_YAML_EVENT_INDICATOR:
            printf("Indicator: '%c'\n", ev->data.indicator);
            break;
            
        case GTEXT_YAML_EVENT_SEQUENCE_START:
            printf("Sequence started\n");
            break;
            
        case GTEXT_YAML_EVENT_MAPPING_START:
            printf("Mapping started\n");
            break;
            
        case GTEXT_YAML_EVENT_DOCUMENT_END:
            printf("Document ended\n");
            break;
            
        case GTEXT_YAML_EVENT_STREAM_END:
            printf("Stream ended\n");
            break;
            
        default:
            printf("Other event type: %d\n", ev->type);
            break;
    }
    
    return GTEXT_YAML_OK;
}

int main(void) {
    const char *yaml_input =
        "name: My Application\n"
        "version: 1.0.0\n"
        "database:\n"
        "  host: localhost\n"
        "  port: 5432\n";
    
    // Create parser with default options
    GTEXT_YAML_Stream *parser = gtext_yaml_stream_new(
        NULL,           // NULL = use default options
        event_callback, // Callback function
        NULL            // User context (not needed here)
    );
    
    if (!parser) {
        fprintf(stderr, "Failed to create YAML parser\n");
        return 1;
    }
    
    // Feed the entire input (could be done in chunks)
    GTEXT_YAML_Status status = gtext_yaml_stream_feed(
        parser,
        yaml_input,
        strlen(yaml_input)
    );
    
    if (status != GTEXT_YAML_OK) {
        fprintf(stderr, "Parse error: status %d\n", status);
        gtext_yaml_stream_free(parser);
        return 1;
    }
    
    // Signal end of input
    status = gtext_yaml_stream_finish(parser);
    if (status != GTEXT_YAML_OK) {
        fprintf(stderr, "Finish error: status %d\n", status);
        gtext_yaml_stream_free(parser);
        return 1;
    }
    
    // Clean up
    gtext_yaml_stream_free(parser);
    
    printf("\nParsing completed successfully!\n");
    return 0;
}
```

## Key API Functions Used

- `gtext_yaml_stream_new()` - Create streaming parser with callback
- `gtext_yaml_stream_feed()` - Feed input data in chunks
- `gtext_yaml_stream_finish()` - Signal end of input and finalize
- `gtext_yaml_stream_free()` - Free parser resources

## Key Concepts

### Event Types

The parser emits these event types:
- `GTEXT_YAML_EVENT_STREAM_START` / `STREAM_END` - Document stream boundaries
- `GTEXT_YAML_EVENT_DOCUMENT_START` / `DOCUMENT_END` - Individual document boundaries
- `GTEXT_YAML_EVENT_SCALAR` - Plain or quoted string values
- `GTEXT_YAML_EVENT_INDICATOR` - Structural characters (`:`, `-`, etc.)
- `GTEXT_YAML_EVENT_SEQUENCE_START` / `SEQUENCE_END` - Array/list boundaries
- `GTEXT_YAML_EVENT_MAPPING_START` / `MAPPING_END` - Object/dictionary boundaries
- `GTEXT_YAML_EVENT_ALIAS` - Reference to an anchor (`*name`)

### Source Location

Every event includes source location metadata:
- `offset` - Byte offset in input
- `line` - Line number (1-based)
- `col` - Column number (1-based)

This is useful for error reporting and debugging.

### Chunk Boundaries

The parser handles arbitrary chunk boundaries. You can:
- Feed the entire input at once
- Feed one byte at a time
- Feed natural chunk sizes (e.g., 4KB buffer reads)

The parser buffers internally as needed.

## Related Examples

- [yaml_config_parser.c](@ref example_yaml_config_parser) - Build config structure from YAML
- [yaml_multidoc.c](@ref example_yaml_multidoc) - Parse multi-document YAML streams
- [yaml_anchors.c](@ref example_yaml_anchors) - Working with anchors and aliases
- [Examples Overview](@ref examples) - Return to examples index

## Status Note

**Current Status:** The streaming parser is implemented and functional (as of February 2026). DOM parsing and writer APIs are also available.
