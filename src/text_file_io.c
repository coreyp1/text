/**
 * @file
 *
 * Shared file I/O helpers, internal to the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef _MSC_VER
#define _XOPEN_SOURCE 600
#endif

#include <errno.h>
#include <stdint.h>
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

#include "text_file_io_internal.h"

/** Starting size for the read buffer, and the floor for each growth step. */
#define GTEXT_FILE_READ_CHUNK (64 * 1024)

GTEXT_INTERNAL_API gtext_file_status gtext_file_read_all(
    const char * path, size_t max_bytes, char ** out_data, size_t * out_len) {
  if (!path || !out_data || !out_len) {
    return GTEXT_FILE_E_OPEN;
  }
  *out_data = NULL;
  *out_len = 0;

  FILE * file = fopen(path, "rb");
  if (!file) {
    return GTEXT_FILE_E_OPEN;
  }

  size_t capacity = GTEXT_FILE_READ_CHUNK;
  size_t used = 0;
  char * buffer = (char *)malloc(capacity);
  if (!buffer) {
    fclose(file);
    return GTEXT_FILE_E_OOM;
  }

  for (;;) {
    if (used == capacity) {
      // Double, but never past the caller's ceiling plus the terminator.
      if (capacity > SIZE_MAX / 2) {
        free(buffer);
        fclose(file);
        return GTEXT_FILE_E_OOM;
      }
      size_t next = capacity * 2;
      char * grown = (char *)realloc(buffer, next);
      if (!grown) {
        free(buffer);
        fclose(file);
        return GTEXT_FILE_E_OOM;
      }
      buffer = grown;
      capacity = next;
    }

    size_t want = capacity - used;
    size_t got = fread(buffer + used, 1, want, file);
    used += got;

    if (max_bytes > 0 && used > max_bytes) {
      free(buffer);
      fclose(file);
      return GTEXT_FILE_E_LIMIT;
    }

    if (got < want) {
      if (ferror(file)) {
        free(buffer);
        fclose(file);
        return GTEXT_FILE_E_READ;
      }
      break; // End of file.
    }
  }

  fclose(file);

  // Room for the terminator, which is not counted in the length.
  //
  // There is always room.  The loop above is left only by its break, which is
  // taken when fread returned fewer bytes than the space remaining, so `used`
  // is then strictly less than `capacity`; and whenever the two are equal at
  // the top of the loop the buffer is doubled before reading again.  A
  // `used == capacity` reallocation used to stand here for the case that
  // cannot arise, which is why coverage reported those lines as never
  // executed.
  buffer[used] = '\0';

  *out_data = buffer;
  *out_len = used;
  return GTEXT_FILE_OK;
}

static int gtext_file_fwrite(void * user, const char * bytes, size_t len) {
  FILE * file = (FILE *)user;
  if (len == 0) {
    return 0;
  }
  return fwrite(bytes, 1, len, file) == len ? 0 : 1;
}

/**
 * @brief Create a temporary file beside @p path, so the later rename is on the
 *        same filesystem and therefore atomic.
 */
static int gtext_file_temp_create(
    const char * path, char ** out_path, FILE ** out_file) {
  size_t path_len = strlen(path);
  static const char suffix[] = ".tmpXXXXXX";
  size_t total = path_len + sizeof suffix;

  char * temp_path = (char *)malloc(total);
  if (!temp_path) {
    return 1;
  }
  memcpy(temp_path, path, path_len);
  memcpy(temp_path + path_len, suffix, sizeof suffix);

#ifdef _MSC_VER
  if (_mktemp_s(temp_path, total) != 0) {
    free(temp_path);
    return 1;
  }
  int fd = _open(temp_path, _O_CREAT | _O_EXCL | _O_BINARY | _O_WRONLY,
      _S_IREAD | _S_IWRITE);
  if (fd < 0) {
    free(temp_path);
    return 1;
  }
  FILE * file = _fdopen(fd, "wb");
  if (!file) {
    _close(fd);
    remove(temp_path);
    free(temp_path);
    return 1;
  }
#else
  int fd = mkstemp(temp_path);
  if (fd < 0) {
    free(temp_path);
    return 1;
  }
  FILE * file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    remove(temp_path);
    free(temp_path);
    return 1;
  }
#endif

  *out_path = temp_path;
  *out_file = file;
  return 0;
}

static int gtext_file_replace(const char * source, const char * dest) {
#ifdef _MSC_VER
  return MoveFileExA(source, dest, MOVEFILE_REPLACE_EXISTING) ? 0 : 1;
#else
  return rename(source, dest);
#endif
}

GTEXT_INTERNAL_API gtext_file_status gtext_file_write_atomic(const char * path,
    int (*emit)(void * ctx, gtext_file_write_cb write, void * write_user),
    void * user) {
  if (!path || !emit) {
    return GTEXT_FILE_E_WRITE;
  }

  char * temp_path = NULL;
  FILE * file = NULL;
  if (gtext_file_temp_create(path, &temp_path, &file) != 0) {
    return GTEXT_FILE_E_OPEN;
  }

  int failed = emit(user, gtext_file_fwrite, file);

  // The content is only safe once it has left stdio and reached the file.
  if (!failed && fflush(file) != 0) {
    failed = 1;
  }
  if (fclose(file) != 0) {
    failed = 1;
  }

  if (failed) {
    remove(temp_path);
    free(temp_path);
    return GTEXT_FILE_E_WRITE;
  }

  if (gtext_file_replace(temp_path, path) != 0) {
    remove(temp_path);
    free(temp_path);
    return GTEXT_FILE_E_WRITE;
  }

  free(temp_path);
  return GTEXT_FILE_OK;
}
