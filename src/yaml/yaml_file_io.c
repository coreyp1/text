/**
 * @file
 *
 * YAML file I/O helpers.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/text/yaml/yaml_core.h>
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_writer.h>

static void set_io_error(GTEXT_YAML_Error *err, const char *message) {
  if (!err) {
    return;
  }
  err->code = GTEXT_YAML_E_INVALID;
  err->message = message;
}

static int file_write_fn(void * user, const char * bytes, size_t len) {
  FILE * file = (FILE *)user;
  size_t written = fwrite(bytes, 1, len, file);
  return written == len ? 0 : 1;
}

GTEXT_API GTEXT_YAML_Document * gtext_yaml_parse_file(
  const char * path,
  const GTEXT_YAML_Parse_Options * options,
  GTEXT_YAML_Error * out_err
) {
  if (!path) {
    if (out_err) {
      out_err->code = GTEXT_YAML_E_INVALID;
      out_err->message = "Path is NULL";
    }
    return NULL;
  }

  FILE * file = fopen(path, "rb");
  if (!file) {
    set_io_error(out_err, "Failed to open file");
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    set_io_error(out_err, "Failed to seek file");
    return NULL;
  }

  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    set_io_error(out_err, "Failed to read file size");
    return NULL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    set_io_error(out_err, "Failed to rewind file");
    return NULL;
  }

  size_t len = (size_t)size;
  char * buffer = (char *)malloc(len + 1);
  if (!buffer) {
    fclose(file);
    if (out_err) {
      out_err->code = GTEXT_YAML_E_OOM;
      out_err->message = "Out of memory reading file";
    }
    return NULL;
  }

  size_t read_bytes = fread(buffer, 1, len, file);
  fclose(file);
  if (read_bytes != len) {
    free(buffer);
    set_io_error(out_err, "Failed to read file contents");
    return NULL;
  }

  buffer[len] = '\0';
  GTEXT_YAML_Document * doc = gtext_yaml_parse(buffer, len, options, out_err);
  free(buffer);
  return doc;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_parse_file_all(
  const char * path,
  const GTEXT_YAML_Parse_Options * options,
  GTEXT_YAML_Document *** out_docs,
  size_t * out_count,
  GTEXT_YAML_Error * out_err
) {
  if (!path || !out_docs || !out_count) {
    if (out_err) {
      out_err->code = GTEXT_YAML_E_INVALID;
      out_err->message = "Invalid arguments";
    }
    return GTEXT_YAML_E_INVALID;
  }

  FILE * file = fopen(path, "rb");
  if (!file) {
    set_io_error(out_err, "Failed to open file");
    return GTEXT_YAML_E_INVALID;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    set_io_error(out_err, "Failed to seek file");
    return GTEXT_YAML_E_INVALID;
  }

  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    set_io_error(out_err, "Failed to read file size");
    return GTEXT_YAML_E_INVALID;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    set_io_error(out_err, "Failed to rewind file");
    return GTEXT_YAML_E_INVALID;
  }

  size_t len = (size_t)size;
  char * buffer = (char *)malloc(len + 1);
  if (!buffer) {
    fclose(file);
    if (out_err) {
      out_err->code = GTEXT_YAML_E_OOM;
      out_err->message = "Out of memory reading file";
    }
    return GTEXT_YAML_E_OOM;
  }

  size_t read_bytes = fread(buffer, 1, len, file);
  fclose(file);
  if (read_bytes != len) {
    free(buffer);
    set_io_error(out_err, "Failed to read file contents");
    return GTEXT_YAML_E_INVALID;
  }

  buffer[len] = '\0';
  size_t count = 0;
  GTEXT_YAML_Document ** docs = gtext_yaml_parse_all(buffer, len, &count, options, out_err);
  free(buffer);
  if (!docs) {
    return out_err ? out_err->code : GTEXT_YAML_E_INVALID;
  }

  *out_docs = docs;
  *out_count = count;
  return GTEXT_YAML_OK;
}

GTEXT_API GTEXT_YAML_Status gtext_yaml_write_file(
  const char * path,
  const GTEXT_YAML_Document * doc,
  const GTEXT_YAML_Write_Options * options,
  GTEXT_YAML_Error * out_err
) {
  if (!path || !doc) {
    if (out_err) {
      out_err->code = GTEXT_YAML_E_INVALID;
      out_err->message = "Invalid arguments";
    }
    return GTEXT_YAML_E_INVALID;
  }

  FILE * file = fopen(path, "wb");
  if (!file) {
    set_io_error(out_err, "Failed to open file for writing");
    return GTEXT_YAML_E_INVALID;
  }

  GTEXT_YAML_Sink sink;
  sink.write = file_write_fn;
  sink.user = file;

  GTEXT_YAML_Status status = gtext_yaml_write_document(doc, &sink, options);
  fclose(file);

  if (status != GTEXT_YAML_OK && out_err) {
    out_err->code = status;
    out_err->message = status == GTEXT_YAML_E_WRITE
        ? "Failed to write YAML file"
        : "Failed to serialize YAML document";
  }

  return status;
}
