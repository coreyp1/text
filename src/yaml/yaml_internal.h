/**
 * @file
 * @brief Internal header for YAML implementation.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef TEXT_SRC_YAML_YAML_INTERNAL_H
#define TEXT_SRC_YAML_YAML_INTERNAL_H

#include <stdlib.h>

#include <ghoti.io/text/yaml/yaml_core.h>

/* Internal structures for the scanner, reader, and parser. */

typedef struct GTEXT_YAML_Reader GTEXT_YAML_Reader;

GTEXT_INTERNAL_API GTEXT_YAML_Reader *gtext_yaml_reader_new(const char *data,
															size_t len);
GTEXT_INTERNAL_API void gtext_yaml_reader_free(GTEXT_YAML_Reader *r);
GTEXT_INTERNAL_API int gtext_yaml_reader_peek(GTEXT_YAML_Reader *r);
GTEXT_INTERNAL_API int gtext_yaml_reader_consume(GTEXT_YAML_Reader *r);
GTEXT_INTERNAL_API size_t gtext_yaml_reader_offset(const GTEXT_YAML_Reader *r);
GTEXT_INTERNAL_API void gtext_yaml_reader_position(const GTEXT_YAML_Reader *r,
												  int *line, int *col);

/* UTF-8 validation and scalar buffer utilities (Task 2.2) */

/* Return 1 if the buffer of length `len` is valid UTF-8, 0 otherwise. */
GTEXT_INTERNAL_API int gtext_utf8_validate(const char *buf, size_t len);

/* Simple dynamic buffer used to assemble scalars that may cross input chunks. */
typedef struct {
	char *data;
	size_t len;
	size_t cap;
} GTEXT_YAML_DynBuf;

GTEXT_INTERNAL_API int gtext_yaml_dynbuf_init(GTEXT_YAML_DynBuf *b);
GTEXT_INTERNAL_API void gtext_yaml_dynbuf_free(GTEXT_YAML_DynBuf *b);
GTEXT_INTERNAL_API int gtext_yaml_dynbuf_append(GTEXT_YAML_DynBuf *b, const char *data, size_t len);

/* Minimal scanner/tokenizer (Task 2.3) */
typedef enum {
	GTEXT_YAML_TOKEN_INDICATOR,
	GTEXT_YAML_TOKEN_SCALAR,
	GTEXT_YAML_TOKEN_EOF,
	GTEXT_YAML_TOKEN_ERROR
} GTEXT_YAML_Token_Type;

typedef struct {
	GTEXT_YAML_Token_Type type;
	/* For INDICATOR: 1-byte char in `c`.
		 For SCALAR: pointer/length into dynbuf (owned by scanner until next token).
	*/
	union {
		char c;
		struct { const char *ptr; size_t len; } scalar;
	} u;
	size_t offset; /* byte offset where token begins */
	int line, col;  /* position */
} GTEXT_YAML_Token;

typedef struct GTEXT_YAML_Scanner GTEXT_YAML_Scanner;

/* Streaming scanner API: create empty scanner, feed chunks, mark finish. */
GTEXT_INTERNAL_API GTEXT_YAML_Scanner *gtext_yaml_scanner_new(void);
GTEXT_INTERNAL_API void gtext_yaml_scanner_free(GTEXT_YAML_Scanner *s);
GTEXT_INTERNAL_API int gtext_yaml_scanner_feed(GTEXT_YAML_Scanner *s, const char *data, size_t len);
GTEXT_INTERNAL_API void gtext_yaml_scanner_finish(GTEXT_YAML_Scanner *s);
GTEXT_INTERNAL_API GTEXT_YAML_Status gtext_yaml_scanner_next(GTEXT_YAML_Scanner *s, GTEXT_YAML_Token *tok, GTEXT_YAML_Error *err);

#endif /* TEXT_SRC_YAML_YAML_INTERNAL_H */
