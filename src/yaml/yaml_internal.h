/**
 * @file
 * @brief Internal header for YAML implementation.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef TEXT_SRC_YAML_YAML_INTERNAL_H
#define TEXT_SRC_YAML_YAML_INTERNAL_H

#include <stdlib.h>
#include <stdint.h>

#include <ghoti.io/text/yaml/yaml_core.h>

/* Forward declarations */
typedef struct GTEXT_YAML_Stream GTEXT_YAML_Stream;

/* Internal structures for the scanner, reader, and parser. */

typedef struct GTEXT_YAML_CharReader GTEXT_YAML_CharReader;

GTEXT_INTERNAL_API GTEXT_YAML_CharReader *gtext_yaml_char_reader_new(
	const char *data,
	size_t len
);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_free(GTEXT_YAML_CharReader *r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_peek(GTEXT_YAML_CharReader *r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_consume(GTEXT_YAML_CharReader *r);
GTEXT_INTERNAL_API size_t gtext_yaml_char_reader_offset(
	const GTEXT_YAML_CharReader *r
);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_position(
	const GTEXT_YAML_CharReader *r,
	int *line,
	int *col
);

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
	GTEXT_YAML_TOKEN_DIRECTIVE,
	GTEXT_YAML_TOKEN_DOCUMENT_START,  /* "---" */
	GTEXT_YAML_TOKEN_DOCUMENT_END,    /* "..." */
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

/* ====================================================================
 * Arena Allocator and Context (Phase 4)
 * ==================================================================== */

/**
 * @brief Arena block structure
 *
 * Each block contains a chunk of memory that can be allocated from.
 * Blocks are linked together to form the arena.
 *
 * WARNING: This struct uses a flexible array member pattern and must
 * always be used as a pointer. Never copy or pass by value.
 */
typedef struct yaml_arena_block {
	struct yaml_arena_block *next;  /* Next block in chain */
	size_t used;                    /* Bytes used in this block */
	size_t size;                    /* Total size of this block */
	char data[1];                   /* Flexible array member (C99 FAM) */
} yaml_arena_block;

/**
 * @brief Arena allocator structure
 *
 * Manages a collection of blocks for efficient bulk allocation.
 * All memory is freed when the arena is destroyed.
 */
typedef struct yaml_arena {
	yaml_arena_block *first;    /* First block in chain */
	yaml_arena_block *current;  /* Current allocation block */
	size_t block_size;          /* Size of new blocks (grows exponentially) */
} yaml_arena;

/**
 * @brief YAML context structure
 *
 * Owns the arena allocator and tracks document-level information.
 * All nodes in a document share the same context.
 */
typedef struct yaml_context {
	yaml_arena *arena;              /* Arena allocator */
	const char *input_buffer;       /* Original input (for future in-situ mode) */
	size_t input_buffer_len;        /* Input length */
	struct ResolverState *resolver; /* Anchor/alias resolver */
	size_t node_count;              /* Total nodes allocated (statistics) */
} yaml_context;

/* Arena API */
GTEXT_INTERNAL_API yaml_arena *yaml_arena_new(void);
GTEXT_INTERNAL_API void yaml_arena_free(yaml_arena *arena);
GTEXT_INTERNAL_API void *yaml_arena_alloc(yaml_arena *arena, size_t size, size_t align);

/* Context API */
GTEXT_INTERNAL_API yaml_context *yaml_context_new(void);
GTEXT_INTERNAL_API void yaml_context_free(yaml_context *ctx);
GTEXT_INTERNAL_API void yaml_context_set_input_buffer(yaml_context *ctx, const char *buf, size_t len);
GTEXT_INTERNAL_API void *yaml_context_alloc(yaml_context *ctx, size_t size, size_t align);

/* ============================================================================
 * Phase 4: DOM Node Structures
 * ============================================================================ */

/**
 * @brief Internal DOM node representation.
 *
 * All DOM nodes are allocated from the context arena. Scalars store their
 * content as strings (type resolution is deferred to Phase 5). Collections
 * use flexible array members for children/pairs.
 *
 * Design decisions:
 * - All scalars are strings initially (no int/float/bool until Phase 5)
 * - Sequences store child pointers in flexible array
 * - Mappings store key-value pairs in flexible array
 * - Anchors stored as strings (resolver tracks identity)
 * - Tags stored as strings (resolver handles semantics)
 */

/* Scalar node (all scalars are strings until Phase 5 type resolution) */
typedef struct {
	GTEXT_YAML_Node_Type type;  /* GTEXT_YAML_STRING initially */
	const char *value;          /* Null-terminated string (arena-allocated) */
	size_t length;              /* Length excluding null terminator */
	bool bool_value;            /* Parsed boolean value */
	int64_t int_value;          /* Parsed integer value */
	double float_value;         /* Parsed floating-point value */
	bool has_timestamp;         /* True if timestamp was parsed */
	bool timestamp_has_time;    /* True if time component is present */
	bool timestamp_tz_specified;/* True if timezone was specified */
	bool timestamp_tz_utc;      /* True if timezone was 'Z' */
	int timestamp_year;
	int timestamp_month;
	int timestamp_day;
	int timestamp_hour;
	int timestamp_minute;
	int timestamp_second;
	int timestamp_nsec;         /* Fractional seconds in nanoseconds */
	int timestamp_tz_offset;    /* Offset in minutes from UTC */
	const char *tag;            /* Optional tag (e.g., "!!str"), NULL if none */
	const char *anchor;         /* Optional anchor name, NULL if none */
} yaml_node_scalar;

/* Sequence node (array of child nodes) */
typedef struct {
	GTEXT_YAML_Node_Type type;  /* GTEXT_YAML_SEQUENCE */
	const char *tag;            /* Optional tag, NULL if none */
	const char *anchor;         /* Optional anchor name, NULL if none */
	size_t count;               /* Number of children */
	GTEXT_YAML_Node *children[1]; /* Flexible array member */
} yaml_node_sequence;

/* Mapping node (array of key-value pairs) */
typedef struct {
	const char *key_tag;        /* Optional tag for key */
	const char *value_tag;      /* Optional tag for value */
	GTEXT_YAML_Node *key;       /* Key node (can be any type per YAML spec) */
	GTEXT_YAML_Node *value;     /* Value node */
} yaml_mapping_pair;

typedef struct {
	const char *handle;
	const char *prefix;
} yaml_tag_handle;

typedef struct {
	GTEXT_YAML_Node_Type type;  /* GTEXT_YAML_MAPPING */
	const char *tag;            /* Optional tag, NULL if none */
	const char *anchor;         /* Optional anchor name, NULL if none */
	size_t count;               /* Number of key-value pairs */
	yaml_mapping_pair pairs[1]; /* Flexible array member */
} yaml_node_mapping;

/* Alias node (references another node by anchor name) */
typedef struct {
	GTEXT_YAML_Node_Type type;  /* GTEXT_YAML_ALIAS */
	const char *anchor_name;    /* Name of the referenced anchor (arena-allocated) */
	GTEXT_YAML_Node *target;    /* Resolved target node (NULL until resolved) */
} yaml_node_alias;

/* Union node type (public type is opaque pointer to this) */
struct GTEXT_YAML_Node {
	GTEXT_YAML_Node_Type type;
	union {
		yaml_node_scalar scalar;
		yaml_node_sequence sequence;
		yaml_node_mapping mapping;
		yaml_node_alias alias;
		/* Note: We use the full structs in the union to avoid pointer chasing,
		 * but this means the union size is determined by the largest member.
		 * For now, we accept this tradeoff for simpler allocation. */
	} as;
};

/* Document structure (root + metadata) */
struct GTEXT_YAML_Document {
	yaml_context *ctx;          /* Owns arena and resolver */
	GTEXT_YAML_Node *root;      /* Root node of the document */
	GTEXT_YAML_Parse_Options options; /* Parse options used */
	size_t node_count;          /* Total nodes allocated (statistics) */
	size_t document_index;      /* Index in multi-document stream (0-based) */
	bool has_directives;        /* True if %YAML or %TAG directives present */
	int yaml_version_major;     /* YAML version from %YAML directive (0 if none) */
	int yaml_version_minor;
	const char *input_newline;  /* Detected line ending for input (if any) */
	yaml_tag_handle *tag_handles; /* Array of %TAG handle mappings */
	size_t tag_handle_count;
};

/* ============================================================================
 * Phase 4: DOM Parser API (Internal)
 * ============================================================================ */

/**
 * @brief Parse YAML string into DOM document.
 *
 * This is the internal implementation of gtext_yaml_parse(). It uses the
 * streaming parser internally and builds a DOM tree in the context arena.
 *
 * @param input Input string (must remain valid for document lifetime)
 * @param length Length of input string
 * @param options Parse options (NULL for defaults)
 * @param error Error output (may be NULL)
 * @return Document on success, NULL on error
 */
GTEXT_INTERNAL_API GTEXT_YAML_Document *yaml_parse_document(
	const char *input,
	size_t length,
	const GTEXT_YAML_Parse_Options *options,
	GTEXT_YAML_Error *error
);

/**
 * @brief Resolve tags and implicit scalar types for a document.
 */
GTEXT_INTERNAL_API GTEXT_YAML_Status yaml_resolve_document(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Error *error
);

/**
 * @brief Node factory functions (allocate from context arena).
 */
GTEXT_INTERNAL_API GTEXT_YAML_Node *yaml_node_new_scalar(
	yaml_context *ctx,
	const char *value,
	size_t length,
	const char *tag,
	const char *anchor
);

GTEXT_INTERNAL_API GTEXT_YAML_Node *yaml_node_new_sequence(
	yaml_context *ctx,
	size_t capacity,
	const char *tag,
	const char *anchor
);

GTEXT_INTERNAL_API GTEXT_YAML_Node *yaml_node_new_mapping(
	yaml_context *ctx,
	size_t capacity,
	const char *tag,
	const char *anchor
);

GTEXT_INTERNAL_API GTEXT_YAML_Node *yaml_node_new_alias(
	yaml_context *ctx,
	const char *anchor_name
);

/* Stream internal API */
GTEXT_INTERNAL_API void gtext_yaml_stream_set_sync_mode(GTEXT_YAML_Stream *s, bool sync);

#endif /* TEXT_SRC_YAML_YAML_INTERNAL_H */
