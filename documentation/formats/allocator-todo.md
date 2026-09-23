@page format_allocator_todo Extending the allocator to CSV and YAML

# Extending the allocator to CSV and YAML

`GTEXT_JSON_Parse_Options::allocator` routes a whole JSON parse through a
caller-supplied `GTEXT_Allocator`, which is cutil's `GCU_Allocator` under a
local name. **`GTEXT_CSV_Parse_Options::allocator` now does the same for CSV.**
YAML still allocates with the C library. This page records what the remaining
work is, because it was started, measured, and deliberately not finished by
halves.

## Why it is not done yet

A partial allocator is worse than none. If `GTEXT_CSV_Parse_Options` gained an
`allocator` field that the arena honored but the table structure did not, then
`gtext_csv_free_table()` would release the arena through the caller's
allocator and the table structure through `free()`. That is not an incomplete
feature; it is heap corruption in a caller who supplies an arena, and the
caller has no way to see it coming.

So the rule is the one `make check-allocators` enforces: a file is added to
`ALLOCATOR_CLEAN_SOURCES` when every allocation in it goes through the
allocator, and the public option is only documented as covering what that list
covers.

## CSV: done

`GTEXT_CSV_Parse_Options::allocator` covers the parse and everything the table
owns afterwards. `src/csv/csv_table.c`, `src/csv/csv_stream.c` and
`src/csv/csv_stream_buffer.c` are in `ALLOCATOR_CLEAN_SOURCES`, so
`make check-allocators` fails on a raw allocation in any of them. 174 sites
were converted.

The arena and the context each carry the allocator; the arena carries its own
copy because `csv_arena_free()` releases the structure the allocator was read
from, so it has to be captured before the loop rather than read inside it. The
three temporary-array holders - `csv_column_op_temp_arrays`,
`csv_compact_structures` and `csv_clone_structures` - carry it too, set before
their first allocation so that an unwind triggered by that first failure still
has a valid allocator to free through. Clones and compactions take the
allocator of the table they came from, so compacting never moves a caller's
data onto the C heap.

Two public entry points were added, because `gtext_csv_new_table()` and
`gtext_csv_new_table_with_headers()` take no options and so have nowhere to
name an allocator: `gtext_csv_new_table_with_allocator()` and
`gtext_csv_new_table_with_headers_and_allocator()`. The old names delegate to
them with `NULL`.

**Two things are deliberately still on the C library, and neither can mix with
the other side.** `GTEXT_CSV_Error` and its context snippet are released by
`gtext_csv_error_free()`, which is handed an error and no allocator - the same
is true of `GTEXT_JSON_Error`. And the writer - sinks, the writer structure,
and the transient escape buffer - is a separate entry point taking write
options, exactly as the JSON writer is. Neither is ever freed through a
caller's allocator or vice versa. The one exempt site inside a clean file
carries an `allocator-exempt` marker, which is what the gate reads.

### What the conversion actually caught

Worth recording, because both were found by a check rather than by reading.

`csv_field_buffer_init()` ends with `memset(fb, 0, sizeof(*fb))`. The
allocator was being assigned to the field buffer *above* that call, so the
memset wiped it: the buffer then grew through the C library and
`gtext_csv_stream_free()` released it through the caller's allocator. The
tracking allocator's guard word caught it as a block it had never made. The
fix was to make the allocator a parameter of the initializer rather than
something a caller assigns afterwards, so the ordering cannot be got wrong
again.

The other was a gap in the tests, not the code. Planting the exact defect this
page describes - the table structure on the C library while its arena comes
from the caller's allocator - left the whole suite green, because
`csv_create_empty_table()` is reached only by parsing zero bytes and nothing
had ever done that. `Allocator.CsvEmptyInputBalancesThroughTheAllocator`
exists for that path, and fails on the planted defect with
"freed a block this allocator never made".

## YAML

Not started, and now the only format left. `src/yaml/yaml_arena.c` is the
equivalent chokepoint and the same shape of change applies. YAML has 296 raw
allocation calls against the 174 CSV needed, and the resolver and the DOM
manipulation functions both allocate outside the arena.

Two lessons from CSV transfer directly. Any initializer that memsets a
structure must take the allocator as a parameter rather than have it assigned
afterwards. And a test that exercises the ordinary path can miss a whole
entry point: the coverage to aim for is one balanced test per way of *creating*
a document, not one per way of using it.

## The other JSON entry points

`GTEXT_JSON_Parse_Options::allocator` covers parsing. The JSON writer, the
streaming parser, JSON Pointer, JSON Patch and JSON Schema each have their own
entry points and take no allocator. Each needs an options structure of its own
or an added parameter; none of them shares the parse options.

## The error-snippet exception

`GTEXT_JSON_Error::context_snippet` and its CSV equivalent stay C-library
memory in every plan above. `gtext_json_error_free()` and
`gtext_csv_error_free()` receive only the error, so they cannot learn which
allocator produced the snippet, and freeing it through the wrong one is worse
than the single diagnostic allocation it would save. Changing that means
putting an allocator in the public error structure, which is a decision about
the API rather than an implementation detail. The sites are marked
`allocator-exempt` in the source so the check passes them deliberately rather
than by omission.

---

Back to \ref format_comparison "Comparison with other libraries".
