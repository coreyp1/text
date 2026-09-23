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
 * The allocating, UTF-8 face of the normaliser.
 *
 * Its own translation unit rather than part of nfc.c, for the same reason its
 * declaration is in its own header: nfc.c must not depend on GTEXT_Allocator.
 * tools/idna/nfc_oracle.py builds a standalone driver that links the archive,
 * and an object file is pulled in whole - so a reference to the allocator from
 * nfc.o drags allocator.o and therefore cutil into a gate that is about UAX #15
 * and nothing else. Both failures were caught by the gate rather than reasoned
 * about, which is the argument for keeping it cheap to run.
 */

#include <stddef.h>
#include <stdint.h>

#include "nfc_internal.h"
#include "nfc_utf8_internal.h"

// ===========================================================================
// The UTF-8 face of it
// ===========================================================================

/*
 * `gtext_nfc` speaks codepoints because the IDNA pipeline already has them:
 * UTS #46 maps a name codepoint by codepoint and normalises what it produced.
 * Every other caller in this library holds UTF-8 instead, and would otherwise
 * have to know the 4x expansion bound to size a buffer - which is a detail of
 * UAX #15 and has no business in a JSON lexer.
 *
 * idna.c has a strict UTF-8 decoder of its own, written for host names. It
 * refuses exactly what this one refuses and the two should converge; they are
 * still two, which is recorded here rather than left to be discovered.
 */

/* Decode one character, or return 0 and leave `pos` alone. An overlong form,
 * a surrogate and a value past U+10FFFF are refused rather than repaired: the
 * caller asked for normalised text, and text that needs repairing first is
 * not the question it asked. */
static uint32_t nfc_utf8_next(const char * s, size_t len, size_t * pos) {
  unsigned char c = (unsigned char)s[*pos];
  size_t extra;
  uint32_t cp;

  if (c < 0x80) {
    *pos += 1;
    return c;
  }
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1Fu;
  }
  else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0Fu;
  }
  else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07u;
  }
  else {
    return 0;
  }
  if (extra > len - *pos - 1) {
    return 0;
  }
  for (size_t i = 1; i <= extra; i++) {
    unsigned char cc = (unsigned char)s[*pos + i];
    if ((cc & 0xC0) != 0x80) {
      return 0;
    }
    cp = (cp << 6) | (cc & 0x3Fu);
  }
  if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800)
      || (extra == 3 && cp < 0x10000)) {
    return 0; // overlong
  }
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return 0;
  }
  *pos += extra + 1;
  return cp;
}

/* Encode one character. Returns how many bytes it wrote; never more than 4,
 * and never called with anything nfc_utf8_next did not already accept. */
static size_t nfc_utf8_put(uint32_t cp, unsigned char * out) {
  if (cp < 0x80) {
    out[0] = (unsigned char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (unsigned char)(0xC0 | (cp >> 6));
    out[1] = (unsigned char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (unsigned char)(0xE0 | (cp >> 12));
    out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (unsigned char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (unsigned char)(0xF0 | (cp >> 18));
  out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (unsigned char)(0x80 | (cp & 0x3F));
  return 4;
}

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

  size_t ncp = 0;
  size_t pos = 0;
  while (pos < len) {
    size_t before = pos;
    uint32_t cp = nfc_utf8_next(in, len, &pos);
    if (cp == 0 && pos == before) {
      /* Malformed. A NUL byte is a real codepoint and advances, so this is
         only ever the decoder's refusal and never a NUL in the string. */
      gtext_allocator_free(alloc, cps);
      gtext_allocator_free(alloc, norm);
      return 0;
    }
    cps[ncp++] = cp;
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
  for (size_t i = 0; i < norm_len; i++) {
    nb += nfc_utf8_put(norm[i], (unsigned char *)bytes + nb);
  }
  bytes[nb] = '\0';
  gtext_allocator_free(alloc, norm);

  *out = bytes;
  *out_len = nb;
  return 1;
}
