/**
 * @file
 *
 * JSON file I/O.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_writer.h>
#include <ghoti.io/text/macros.h>

#include "../text_file_io_internal.h"
#include "json_internal.h"

/** Map a shared file status onto this module's codes and message. */
static GTEXT_JSON_Status json_file_error(
    gtext_file_status status, GTEXT_JSON_Error * err) {
  GTEXT_JSON_Status code;
  const char * message;
  switch (status) {
  case GTEXT_FILE_E_OPEN:
    code = GTEXT_JSON_E_INVALID;
    message = "Failed to open file";
    break;
  case GTEXT_FILE_E_READ:
    code = GTEXT_JSON_E_INVALID;
    message = "Failed to read file";
    break;
  case GTEXT_FILE_E_WRITE:
    code = GTEXT_JSON_E_WRITE;
    message = "Failed to write file";
    break;
  case GTEXT_FILE_E_OOM:
    code = GTEXT_JSON_E_OOM;
    message = "Out of memory reading file";
    break;
  case GTEXT_FILE_E_LIMIT:
    code = GTEXT_JSON_E_LIMIT;
    message = "File exceeds max_total_bytes";
    break;
  default:
    code = GTEXT_JSON_E_INVALID;
    message = "File operation failed";
    break;
  }
  if (err) {
    err->code = code;
    err->message = message;
    err->line = 1;
    err->col = 1;
  }
  return code;
}

GTEXT_API GTEXT_JSON_Value * gtext_json_parse_file(const char * path,
    const GTEXT_JSON_Parse_Options * opts, GTEXT_JSON_Error * err) {
  if (!path) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Path must not be NULL",
          .line = 1,
          .col = 1};
    }
    return NULL;
  }

  GTEXT_JSON_Parse_Options effective =
      opts ? *opts : gtext_json_parse_options_default();
  size_t max_bytes = effective.max_total_bytes ? effective.max_total_bytes
                                               : JSON_DEFAULT_MAX_TOTAL_BYTES;

  char * data = NULL;
  size_t len = 0;
  gtext_file_status fs = gtext_file_read_all(path, max_bytes, &data, &len);
  if (fs != GTEXT_FILE_OK) {
    json_file_error(fs, err);
    return NULL;
  }

  // in_situ_mode would leave the value pointing into a buffer this function
  // owns and is about to free, so it cannot be honored here.
  effective.in_situ_mode = false;

  GTEXT_JSON_Value * value = gtext_json_parse(data, len, &effective, err);
  gtext_file_free(data);
  return value;
}

/** Context for the atomic write callback. */
typedef struct {
  const GTEXT_JSON_Value * value;
  const GTEXT_JSON_Write_Options * opts;
  GTEXT_JSON_Status status;
} json_file_write_ctx;

/** The shared sink and this module's sink have the same shape already. */
static int json_file_emit(
    void * ctx, gtext_file_write_cb write, void * write_user) {
  json_file_write_ctx * c = (json_file_write_ctx *)ctx;

  GTEXT_JSON_Sink sink;
  sink.write = write;
  sink.user = write_user;

  c->status = gtext_json_write_value(&sink, c->opts, c->value, NULL);
  return c->status == GTEXT_JSON_OK ? 0 : 1;
}

GTEXT_API GTEXT_JSON_Status gtext_json_write_file(const char * path,
    const GTEXT_JSON_Value * value, const GTEXT_JSON_Write_Options * opts,
    GTEXT_JSON_Error * err) {
  if (!path || !value) {
    if (err) {
      *err = (GTEXT_JSON_Error){.code = GTEXT_JSON_E_INVALID,
          .message = "Path and value must not be NULL",
          .line = 1,
          .col = 1};
    }
    return GTEXT_JSON_E_INVALID;
  }

  json_file_write_ctx ctx;
  ctx.value = value;
  ctx.opts = opts;
  ctx.status = GTEXT_JSON_OK;

  gtext_file_status fs = gtext_file_write_atomic(path, json_file_emit, &ctx);
  if (fs != GTEXT_FILE_OK) {
    // A failure inside the serializer is more specific than "write failed".
    if (ctx.status != GTEXT_JSON_OK) {
      if (err) {
        err->code = ctx.status;
        err->message = "Failed to serialize value";
      }
      return ctx.status;
    }
    return json_file_error(fs, err);
  }
  return GTEXT_JSON_OK;
}
