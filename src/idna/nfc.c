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
 * Unicode normalisation form C, for UTS #46's second step.
 *
 * The algorithm and its tables are ghoti.io-unicode's. This file is the
 * adapter: it maps that library's GUNI_Result onto the 1/0 this repository's
 * internal callers were written against, and it states the one limit choice
 * the migration had to make.
 *
 * What was here before: UAX #15's recursive decomposition, the canonical
 * ordering, the blocking rule, and the composition exclusions, over 1,344
 * lines of tables generated from UnicodeData.txt by tools/idna/gen_uts46.py.
 * regex generated the same tables from the same file with its own generator,
 * and font would have been the third copy; the tables agreed only because
 * both happened to pin UCD 17.0.0, which nothing checked. They are one copy
 * now, in one library, with one pin.
 *
 * The behaviour did not change, and that is measured rather than assumed:
 * every codepoint on its own, a starter with each of the 968 marks that have
 * a non-zero combining class, starter-with-two-marks over a spread of both,
 * and every L/V/T jamo combination. tools/oracle/nfc_diff.py still compiles
 * against this header and still answers to CPython.
 *
 * The figure that belongs here is the one that gate prints, and it depends on
 * which CPython answers, so it names the pin: against a reference carrying the
 * same UCD 17.0.0 these tables do, 3,596,802 of 4,411,532 sequences compared
 * with no disagreement, the remaining 814,730 being sequences that contain a
 * codepoint 17.0.0 leaves unassigned. An earlier revision of this comment said
 * 1,202,634, which matched nothing the gate has ever printed.
 */

#include <stdint.h>

#include <ghoti.io/unicode/core.h>
#include <ghoti.io/unicode/norm.h>

#include "nfc_internal.h"

GTEXT_INTERNAL_API int gtext_nfc(const uint32_t * in, size_t len,
    uint32_t * out, size_t cap, size_t * out_len) {
  if (!in || !out || !out_len) {
    return 0;
  }

  /* guni_normalize() caps the text it will walk at 64 MiB by default, and
     refuses past it. Nothing here had such a cap: a caller that normalised a
     70 MiB JSON string got it normalised, and a migration is not the place to
     start refusing what used to work. The other two fields keep their
     defaults, which are the Standard's own bounds rather than a policy -
     UAX #9's embedding depth of 125 and UAX #15 section 13's 30 non-starters
     - so they are read from guni_limits_default() rather than zeroed. */
  GUNI_Limits limits;
  guni_limits_default(&limits);
  limits.max_text_bytes = SIZE_MAX;

  return guni_normalize(GUNI_NFC, in, len, &limits, out, cap, out_len)
      == GUNI_OK;
}
