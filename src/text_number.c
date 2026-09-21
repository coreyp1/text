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

/* Locale support, the same test the JSON writer used to carry. */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
#include <locale.h>
#define GTEXT_HAVE_USELOCALE 1
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <xlocale.h>
#define GTEXT_HAVE_USELOCALE 1
#else
#include <locale.h>
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
