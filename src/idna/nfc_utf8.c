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
 * The UTF-8 face of NFC: decode, normalise, encode, allocating the result.
 *
 * All three steps are ghoti.io-unicode's now. What this file keeps is the
 * part that is this library's own: the allocator, the "always allocates"
 * ownership rule, and the two-way failure report its callers read.
 *
 * It decodes to codepoints and back rather than calling
 * guni_normalize_utf8(), which is the shorter route, because that entry point
 * works between normalisation boundaries with a fixed working buffer and
 * refuses a run of more than about a thousand combining marks on one base.
 * No natural text does that, but a JSON document can contain it and this
 * function used to normalise it - the codepoint form has no such bound. The
 * shape is therefore the one that was here before, with the hand-written
 * UTF-8 decoder and encoder replaced by the library's.
 */

#include <stdint.h>
#include <stdlib.h>

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/norm.h>
#include <ghoti.io/unicode/utf.h>

#include "nfc_internal.h"
#include "nfc_utf8_internal.h"

GTEXT_INTERNAL_API int gtext_nfc_utf8(const GTEXT_Allocator * alloc,
    const char * in, size_t len, char ** out, size_t * out_len) {
  if (!in || !out || !out_len) {
    return 0;
  }
  *out = NULL;
  *out_len = 0;

  /* An empty string normalises to itself, and still allocates: a caller that
     has to ask "did this one give me a buffer to free?" gets that wrong. */
  if (len == 0) {
    char * empty = (char *)gtext_allocator_malloc(alloc, 1);
    if (!empty) {
      return -1;
    }
    empty[0] = '\0';
    *out = empty;
    return 1;
  }

  /* One codepoint is at least one byte, so `len` of them is always enough to
     hold the input; UAX #15 gives four characters as the largest canonical
     expansion of one, so 4x that holds the decomposed form gtext_nfc needs
     room for. Both products are checked rather than assumed - a string long
     enough to overflow them is a refusal, not a wrap. */
  if (len > SIZE_MAX / 4 / sizeof(uint32_t)) {
    return -1;
  }
  size_t cp_cap = len * 4;
  uint32_t * cps = (uint32_t *)gtext_allocator_malloc(
      alloc, len * sizeof(uint32_t));
  uint32_t * norm = (uint32_t *)gtext_allocator_malloc(
      alloc, cp_cap * sizeof(uint32_t));
  if (!cps || !norm) {
    gtext_allocator_free(alloc, cps);
    gtext_allocator_free(alloc, norm);
    return -1;
  }

  /* REFUSE, not REPLACE: an ill-formed byte was a refusal here before, and a
     silent U+FFFD would make a malformed name normalise to a well-formed one
     that the caller never sent. */
  size_t ncp = 0;
  if (guni_utf8_to_codepoints(in, len, GUNI_INVALID_REFUSE, cps, len, &ncp)
      != GUNI_OK) {
    gtext_allocator_free(alloc, cps);
    gtext_allocator_free(alloc, norm);
    return 0;
  }

  size_t norm_len = 0;
  if (!gtext_nfc(cps, ncp, norm, cp_cap, &norm_len)) {
    gtext_allocator_free(alloc, cps);
    gtext_allocator_free(alloc, norm);
    return -1;
  }
  gtext_allocator_free(alloc, cps);

  /* Four bytes per codepoint is the encoder's ceiling. */
  if (norm_len > (SIZE_MAX - 1) / 4) {
    gtext_allocator_free(alloc, norm);
    return -1;
  }
  char * bytes = (char *)gtext_allocator_malloc(alloc, norm_len * 4 + 1);
  if (!bytes) {
    gtext_allocator_free(alloc, norm);
    return -1;
  }
  size_t nb = 0;
  if (guni_utf8_from_codepoints(norm, norm_len, GUNI_INVALID_REFUSE, bytes,
          norm_len * 4, &nb) != GUNI_OK) {
    gtext_allocator_free(alloc, norm);
    gtext_allocator_free(alloc, bytes);
    return -1;
  }
  bytes[nb] = '\0';
  gtext_allocator_free(alloc, norm);

  *out = bytes;
  *out_len = nb;
  return 1;
}
