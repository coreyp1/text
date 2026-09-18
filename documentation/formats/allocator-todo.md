@page format_allocator_todo Extending the allocator to CSV and YAML

# Extending the allocator to CSV and YAML

`GTEXT_JSON_Parse_Options::allocator` routes a whole JSON parse through a
caller-supplied `GTEXT_Allocator`. CSV and YAML still allocate with the C
library. This page records what the remaining work is, because it was started,
measured, and deliberately not finished by halves.

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

## CSV: what is done and what remains

Converting the arena is straightforward and was done once already:
`csv_arena` and `csv_context` each gain an `alloc` member, `csv_context_new()`
takes the allocator, `csv_arena_free()` captures it before releasing the arena
that holds it, and `gtext_csv_parse_table()` passes `opts->allocator`. Clones
and compactions take the allocator of the table they came from, so compacting
never moves a caller's data into the C heap.

The streaming parser is also straightforward: the stream structure, the field
buffer and the field-buffer growth path, with the allocator carried in the
field-buffer structure so `csv_field_buffer_grow()` reaches it without a
signature change.

**What stops it being finished in one pass is `src/csv/csv_table.c`.** Roughly
160 further allocation and free sites remain there, in three groups:

- The `GTEXT_CSV_Table` structure itself and the header map, allocated in
  `gtext_csv_new_table()`, `gtext_csv_new_table_with_headers()` and the two
  parse entry points.
- The temporary arrays in the column-insert, column-append and
  normalize paths - `new_field_arrays`, `new_field_data_ptrs`,
  `field_data_array`, `field_data_lengths` and their unwind paths.
- The clone and compact paths, which build a second set of the same
  structures before swapping them in.

Each site has a table or a context in scope, so `table->ctx->alloc` reaches the
allocator in almost every case; the work is mechanical rather than difficult.
It is the volume, and the fact that a single site left on the C library
corrupts the heap rather than merely leaking, that makes it a task of its own
rather than something to do at the end of an afternoon.

## YAML

Not started. `src/yaml/yaml_arena.c` is the equivalent chokepoint and the same
shape of change applies. YAML has 66 allocation sites against CSV's 44, and
the resolver and the DOM manipulation functions both allocate outside the
arena.

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
