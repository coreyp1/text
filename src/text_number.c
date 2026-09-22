/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Locale-independent number conversion.
 *
 * See text_number_internal.h for why this is shared rather than per-format.
 */

/* Locale support.
 *
 * This was "the same test the JSON writer used to carry", and the test was
 * never true. It read
 *
 *     #if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
 *
 * placed here, above every #include in the file - and _POSIX_C_SOURCE is
 * defined *by* features.h, which arrives with the first include. At the point
 * that #if was evaluated the macro did not exist yet, so the test was false on
 * every platform, always. Nothing supplied it from outside either: the
 * Makefile passes no -D_POSIX_C_SOURCE, and the build is -std=c17, which is
 * strict-ANSI and would not have glibc volunteer it.
 *
 * The #elif that followed tested __APPLE__ and __FreeBSD__, which are
 * *compiler* predefines and therefore are live before any include. So the
 * chain selected uselocale on macOS and FreeBSD and the fallback on Linux -
 * the one platform this library is developed, tested, fuzzed and shipped on,
 * and the only one whose arm was chosen by a test that could not be true.
 *
 * It compiled clean, passed -Werror, and cost no correctness: the fallback
 * below saves and restores LC_NUMERIC and converts properly. What it cost was
 * the reason uselocale was wanted. setlocale is process-wide, so every JSON
 * and YAML number conversion briefly moved the whole program's locale and any
 * other thread formatting output in that window saw the wrong separator -
 * which makes the thread-safety this library documents ("two that were
 * created separately share nothing and may be used concurrently") false. No
 * crash, no leak, nothing for a sanitizer to find.
 *
 * Two changes. The define goes *before* the include so features.h sees it,
 * and the test asks after LC_NUMERIC_MASK - the thing the code below actually
 * uses - rather than a standards level that is supposed to imply it. glibc
 * declares that macro only where newlocale is declared, so the guard cannot
 * drift from what the guarded code needs; a standards-level test can be right
 * today and wrong after a flag change.
 *
 * Found by the libs/model session, which had copied this idiom from here and
 * hit it from the other side: model's fallback arm is inert, so its locale
 * test failed the moment the wrong arm compiled. Ours is a working
 * alternative, so no before-and-after assertion could tell the two apart -
 * gtext_number_is_thread_local() exists because of that. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <locale.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
/* Compiler predefines, so unlike the test above these are live here. */
#include <xlocale.h>
#endif

#ifdef LC_NUMERIC_MASK
#define GTEXT_HAVE_USELOCALE 1
#else
#define GTEXT_HAVE_USELOCALE 0
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text_number_internal.h"

#if GTEXT_HAVE_USELOCALE

/* uselocale changes the locale of the calling thread only, so a conversion
   here cannot disturb another thread mid-print. */
GTEXT_INTERNAL_API int gtext_number_format(
    char * buf, size_t buf_size, const char * format, ...) {
  if (!buf || !format || buf_size == 0) {
    return -1;
  }

  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (!c_locale) {
    return -1;
  }
  locale_t old_locale = uselocale(c_locale);

  va_list args;
  va_start(args, format);
  int result = vsnprintf(buf, buf_size, format, args);
  va_end(args);

  uselocale(old_locale);
  freelocale(c_locale);

  return result;
}

GTEXT_INTERNAL_API double gtext_number_strtod(const char * s, char ** end) {
  if (!s) {
    if (end) *end = NULL;
    return 0.0;
  }

  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (!c_locale) {
    /* Better a conversion in the caller's locale than none at all: it is
       right wherever the separator is "." and no worse than before here. */
    return strtod(s, end);
  }
  locale_t old_locale = uselocale(c_locale);

  /* errno is the caller's to read, and uselocale must not be what sets it. */
  const int saved_errno = errno;
  errno = 0;
  double result = strtod(s, end);
  const int conversion_errno = errno;

  uselocale(old_locale);
  freelocale(c_locale);

  errno = conversion_errno ? conversion_errno : saved_errno;
  return result;
}

#else

/* setlocale is process-wide: this fallback is for platforms with no
   per-thread locale, where there is nothing better to be had. */
static char * save_numeric_locale(void) {
  const char * current = setlocale(LC_NUMERIC, NULL);
  if (!current) return NULL;
  size_t len = strlen(current);
  if (len == SIZE_MAX) return NULL;
  char * saved = (char *)malloc(len + 1);
  if (!saved) return NULL;
  memcpy(saved, current, len + 1);
  return saved;
}

static void restore_numeric_locale(char * saved) {
  if (saved) {
    setlocale(LC_NUMERIC, saved);
    free(saved);
  }
  else {
    setlocale(LC_NUMERIC, "C");
  }
}

GTEXT_INTERNAL_API int gtext_number_format(
    char * buf, size_t buf_size, const char * format, ...) {
  if (!buf || !format || buf_size == 0) {
    return -1;
  }

  char * saved = save_numeric_locale();
  setlocale(LC_NUMERIC, "C");

  va_list args;
  va_start(args, format);
  int result = vsnprintf(buf, buf_size, format, args);
  va_end(args);

  restore_numeric_locale(saved);
  return result;
}

GTEXT_INTERNAL_API double gtext_number_strtod(const char * s, char ** end) {
  if (!s) {
    if (end) *end = NULL;
    return 0.0;
  }

  char * saved = save_numeric_locale();
  setlocale(LC_NUMERIC, "C");

  const int saved_errno = errno;
  errno = 0;
  double result = strtod(s, end);
  const int conversion_errno = errno;

  restore_numeric_locale(saved);

  errno = conversion_errno ? conversion_errno : saved_errno;
  return result;
}

#endif

/**
 * @brief Whether the conversions above pin the locale per thread.
 *
 * The only cheap way to tell which arm compiled. Both arms convert correctly,
 * and the fallback restores what it changed, so at every quiescent point the
 * two are indistinguishable - the difference exists only *during* a
 * conversion, in another thread. A behavioural test therefore measures the
 * promise; this measures the guard.
 */
GTEXT_INTERNAL_API bool gtext_number_is_thread_local(void) {
  return GTEXT_HAVE_USELOCALE != 0;
}
