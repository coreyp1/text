@page example_json_stream json_stream.c - Streaming Parser

# json_stream.c - Streaming Parser

This example demonstrates how to use the streaming parser for incremental JSON processing. The streaming parser is ideal for processing large JSON documents without building a full DOM in memory.

## What This Example Demonstrates

- **Using the streaming parser** - Process JSON incrementally as it arrives
- **Handling events** - Respond to parser events (object begin/end, array begin/end, values)
- **Processing large documents** - Handle JSON that's too large for memory
- **Multi-chunk input** - Process JSON that arrives in multiple chunks

## Use Case

Use this example when you need to:
- Process large JSON files or network streams
- Transform JSON on-the-fly without building a full DOM
- Handle NDJSON (newline-delimited JSON) streams
- Minimize memory usage for JSON processing

## Source Code

@include examples/json/json_stream.c

## Key API Functions Used

- `gtext_json_stream_new()` - Create a new streaming parser
- `gtext_json_stream_feed()` - Feed input data to the parser (can be called multiple times)
- `gtext_json_stream_finish()` - Signal that all input has been fed
- `gtext_json_stream_free()` - Free the streaming parser

## Important Notes

**Always call `gtext_json_stream_finish()`** after feeding all input chunks. The last value may not be emitted until `finish()` is called, especially if it was incomplete at the end of the final chunk.

## Related Examples

- [json_basic.c](@ref example_json_basic) - Basic DOM parsing
- [json_file_io.c](@ref example_json_file_io) - File I/O with streaming
- [Examples Overview](@ref examples) - Return to examples index
