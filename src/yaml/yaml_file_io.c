/**
 * @file
 *
 * YAML file I/O helpers.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef _MSC_VER
#define _XOPEN_SOURCE 600
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <ghoti.io/text/yaml/yaml_core.h>
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_writer.h>

#include "yaml_internal.h"

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

static const char *detect_input_newline(const char *buffer, size_t len) {
  if (!buffer || len == 0) {
    return NULL;
  }

  for (size_t i = 0; i < len; i++) {
    if (buffer[i] == '\r') {
      if (i + 1 < len && buffer[i + 1] == '\n') {
        return "\r\n";
      }
      return "\r";
    }
    if (buffer[i] == '\n') {
      return "\n";
    }
  }

  return NULL;
}

static void set_document_newline(GTEXT_YAML_Document *doc, const char *newline) {
  if (!doc || !newline) {
    return;
  }
  doc->input_newline = newline;
}

static int replace_file_path(const char *source, const char *dest) {
#ifdef _MSC_VER
  return MoveFileExA(source, dest, MOVEFILE_REPLACE_EXISTING) ? 0 : 1;
#else
  return rename(source, dest);
#endif
}

static int create_temp_file(const char *path, char **out_path, FILE **out_file) {
  if (!path || !out_path || !out_file) {
    return 1;
  }

  size_t path_len = strlen(path);
  const char *suffix = ".tmpXXXXXX";
  size_t suffix_len = strlen(suffix);
  char *temp_path = (char *)malloc(path_len + suffix_len + 1);
  if (!temp_path) {
    return 1;
  }
  snprintf(temp_path, path_len + suffix_len + 1, "%s%s", path, suffix);

#ifdef _MSC_VER
  if (_mktemp_s(temp_path, path_len + suffix_len + 1) != 0) {
    free(temp_path);
    return 1;
  }
  int fd = _open(temp_path, _O_CREAT | _O_EXCL | _O_BINARY | _O_WRONLY, _S_IREAD | _S_IWRITE);
  if (fd < 0) {
    free(temp_path);
    return 1;
  }
  FILE *file = _fdopen(fd, "wb");
  if (!file) {
    _close(fd);
    free(temp_path);
    return 1;
  }
#else
  int fd = mkstemp(temp_path);
  if (fd < 0) {
    free(temp_path);
    return 1;
  }
  FILE *file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    free(temp_path);
    return 1;
  }
#endif

  *out_path = temp_path;
  *out_file = file;
  return 0;
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
  const char *newline = detect_input_newline(buffer, len);
  GTEXT_YAML_Document * doc = gtext_yaml_parse(buffer, len, options, out_err);
  set_document_newline(doc, newline);
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
  const char *newline = detect_input_newline(buffer, len);
  size_t count = 0;
  GTEXT_YAML_Document ** docs = gtext_yaml_parse_all(buffer, len, &count, options, out_err);
  free(buffer);
  if (!docs) {
    return out_err ? out_err->code : GTEXT_YAML_E_INVALID;
  }

  for (size_t i = 0; i < count; i++) {
    set_document_newline(docs[i], newline);
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

  GTEXT_YAML_Write_Options local_opts;
  const GTEXT_YAML_Write_Options *use_opts = options;
  if (!use_opts) {
    local_opts = gtext_yaml_write_options_default();
    if (doc->input_newline) {
      local_opts.newline = doc->input_newline;
    }
    use_opts = &local_opts;
  } else if (!use_opts->newline && doc->input_newline) {
    local_opts = *use_opts;
    local_opts.newline = doc->input_newline;
    use_opts = &local_opts;
  }

  char *temp_path = NULL;
  FILE *file = NULL;
  if (create_temp_file(path, &temp_path, &file) != 0) {
    set_io_error(out_err, "Failed to create temporary file");
    return GTEXT_YAML_E_INVALID;
  }

  GTEXT_YAML_Sink sink;
  sink.write = file_write_fn;
  sink.user = file;

  GTEXT_YAML_Status status = gtext_yaml_write_document(doc, &sink, use_opts);
  if (status == GTEXT_YAML_OK && fflush(file) != 0) {
    status = GTEXT_YAML_E_WRITE;
  }

  if (fclose(file) != 0 && status == GTEXT_YAML_OK) {
    status = GTEXT_YAML_E_WRITE;
  }

  if (status != GTEXT_YAML_OK) {
    remove(temp_path);
    free(temp_path);
    if (out_err) {
      out_err->code = status;
      out_err->message = status == GTEXT_YAML_E_WRITE
          ? "Failed to write YAML file"
          : "Failed to serialize YAML document";
    }
    return status;
  }

  if (replace_file_path(temp_path, path) != 0) {
    remove(temp_path);
    free(temp_path);
    set_io_error(out_err, "Failed to replace output file");
    return GTEXT_YAML_E_INVALID;
  }

  free(temp_path);
  return GTEXT_YAML_OK;
}
