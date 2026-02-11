/**
 * @file yaml_stream.h
 * @brief Streaming (event-driven) YAML parser API.
 *
 * The streaming API accepts chunks of input and emits events via a
 * user-provided callback. This allows parsing large or incremental
 * inputs without needing to build a full DOM.
 */

#ifndef GHOTI_IO_TEXT_YAML_STREAM_H
#define GHOTI_IO_TEXT_YAML_STREAM_H

#include <ghoti.io/text/yaml/yaml_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GTEXT_YAML_Stream GTEXT_YAML_Stream;

/**
 * @brief Event callback invoked by the streaming parser.
 *
 * The callback receives the parser instance @p s, a pointer to an
 * event-specific payload (opaque here), and the user-provided context.
 * The callback should return a @ref GTEXT_YAML_Status; returning a
 * non-zero status will abort parsing and be propagated to the caller.
 */
typedef GTEXT_YAML_Status (*GTEXT_YAML_Event_Callback)(GTEXT_YAML_Stream * s, const void * event_payload, void * user);

/**
 * @enum GTEXT_YAML_Event_Type
 * @brief Event kinds emitted by the streaming parser.
 */
typedef enum {
	GTEXT_YAML_EVENT_STREAM_START,
	GTEXT_YAML_EVENT_STREAM_END,
	GTEXT_YAML_EVENT_DOCUMENT_START,
	GTEXT_YAML_EVENT_DOCUMENT_END,
	GTEXT_YAML_EVENT_SEQUENCE_START,
	GTEXT_YAML_EVENT_SEQUENCE_END,
	GTEXT_YAML_EVENT_MAPPING_START,
	GTEXT_YAML_EVENT_MAPPING_END,
	GTEXT_YAML_EVENT_SCALAR,
	GTEXT_YAML_EVENT_ALIAS,
	GTEXT_YAML_EVENT_INDICATOR,
} GTEXT_YAML_Event_Type;

/**
 * @struct GTEXT_YAML_Event
 * @brief Payload for streaming events.
 *
 * For scalar events the `scalar` field is valid. For indicator events,
 * the `indicator` field holds the single-character indicator.
 */
typedef struct {
	GTEXT_YAML_Event_Type type;
	union {
		struct {
			const char * ptr;
			size_t len;
		} scalar;
		char indicator;
	} data;
	size_t offset;
	int line;
	int col;
} GTEXT_YAML_Event;

/**
 * @brief Create a new streaming parser.
 *
 * The caller may pass NULL for @p opts to use library defaults. The
 * provided callback @p cb will be called for each parser event. Returns
 * a heap-allocated parser object which must be freed with
 * @ref gtext_yaml_stream_free().
 */
GTEXT_API GTEXT_YAML_Stream * gtext_yaml_stream_new(const GTEXT_YAML_Parse_Options * opts, GTEXT_YAML_Event_Callback cb, void * user);

/**
 * @brief Feed a chunk of input to the streaming parser.
 *
 * The parser accepts arbitrary chunk boundaries; it may buffer input
 * internally if a token or block scalar spans multiple calls. Returns
 * GTEXT_YAML_OK on success or an error status. If the callback returns
 * an error, that status will be returned from this function.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_feed(GTEXT_YAML_Stream * s, const char * data, size_t len);

/**
 * @brief Notify the parser that no more input will arrive and finish parsing.
 *
 * This allows the parser to validate final state (e.g., unclosed
 * block scalars) and emit remaining events. Returns GTEXT_YAML_OK on
 * success or an error status.
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_stream_finish(GTEXT_YAML_Stream * s);

/**
 * @brief Free the streaming parser and its internal buffers.
 *
 * Passing NULL is a no-op.
 */
GTEXT_API void gtext_yaml_stream_free(GTEXT_YAML_Stream * s);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_STREAM_H
