/*
 * Internal YAML header exposed for test builds.
 * This mirrors `src/yaml/yaml_internal.h` and is intentionally minimal.
 */
#ifndef GHOTI_IO_TEXT_YAML_INTERNAL_H
#define GHOTI_IO_TEXT_YAML_INTERNAL_H

#include <stddef.h>
#include <ghoti.io/text/macros.h>

typedef struct GTEXT_YAML_CharReader GTEXT_YAML_CharReader;

GTEXT_INTERNAL_API GTEXT_YAML_CharReader * gtext_yaml_char_reader_new(
	const char * data,
	size_t len
);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_free(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_peek(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_consume(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API size_t gtext_yaml_char_reader_offset(const GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_position(
	const GTEXT_YAML_CharReader * r,
	int * line,
	int * col
);

#endif // GHOTI_IO_TEXT_YAML_INTERNAL_H
