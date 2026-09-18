/**
 * @file
 *
 * Shared file I/O helpers, internal to the library.
 *
 * Every format wants the same two things - read a whole document in, write a
 * whole document out without leaving a half-written file behind - and the
 * differences between them are in the parsing, not the plumbing. Keeping the
 * plumbing here means a fix to it reaches all three rather than one.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_TEXT_FILE_IO_INTERNAL_H
#define GHOTI_IO_GTEXT_TEXT_FILE_IO_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Why a file operation failed, without naming any format's status enum.
 *
 * Each module maps these onto its own codes, so the shared layer does not have
 * to know whether it is serving JSON, CSV or YAML.
 */
typedef enum {
  GTEXT_FILE_OK = 0,
  GTEXT_FILE_E_OPEN,  ///< Could not open the path
  GTEXT_FILE_E_READ,  ///< Opened, but reading failed part way
  GTEXT_FILE_E_WRITE, ///< Writing or committing failed
  GTEXT_FILE_E_OOM,   ///< Allocation failed
  GTEXT_FILE_E_LIMIT  ///< File larger than the caller allows
} gtext_file_status;

/**
 * @brief Read an entire file into a NUL-terminated heap buffer.
 *
 * Reads incrementally rather than seeking to the end first, so a pipe, a
 * FIFO, /dev/stdin and anything else without a size still work. The
 * terminator is written past @p out_len and is not counted in it; parsers here
 * all take an explicit length, but a terminator costs one byte and removes a
 * whole class of caller mistake.
 *
 * @param path      File to read.
 * @param max_bytes Refuse anything larger, or 0 for no limit.
 * @param out_data  Receives the buffer; the caller frees it.
 * @param out_len   Receives the length in bytes, terminator excluded.
 * @return GTEXT_FILE_OK, or the reason it failed.
 */
GTEXT_INTERNAL_API gtext_file_status gtext_file_read_all(
    const char * path, size_t max_bytes, char ** out_data, size_t * out_len);

/**
 * @brief Callback that writes one buffer, returning 0 on success.
 */
typedef int (*gtext_file_write_cb)(void * user, const char * bytes, size_t len);

/**
 * @brief Write a file atomically: fully replaced, or not touched at all.
 *
 * The content goes to a temporary file beside the destination, is flushed and
 * closed, and only then replaces it by rename. A caller interrupted half way
 * through - or a full disk - leaves the previous file intact rather than
 * truncated, which matters for exactly the configuration files these parsers
 * are usually pointed at.
 *
 * @param path    Destination path.
 * @param emit    Called once with a sink to write through.
 * @param user    Passed back to @p emit.
 * @return GTEXT_FILE_OK, or the reason it failed.
 */
GTEXT_INTERNAL_API gtext_file_status gtext_file_write_atomic(const char * path,
    int (*emit)(void * ctx, gtext_file_write_cb write, void * write_user),
    void * user);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_TEXT_FILE_IO_INTERNAL_H
