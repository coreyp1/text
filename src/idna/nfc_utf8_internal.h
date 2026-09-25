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
 * Separate from nfc_internal.h on purpose. That header declares nothing but
 * codepoint arrays and includes nothing but stddef and stdint, which is what
 * lets tools/oracle/nfc_diff.py compile a standalone driver against it with no
 * dependency on cutil. Pulling GTEXT_Allocator in there broke that gate -
 * GTEXT_Allocator is a typedef of cutil's GCU_Allocator and cannot be forward
 * declared - so the wrapper that needs an allocator lives here instead and the
 * oracle keeps its short compile line.
 */

#ifndef GHOTI_IO_GTEXT_SRC_IDNA_NFC_UTF8_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_IDNA_NFC_UTF8_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Normalise a UTF-8 string to NFC, allocating the result.
 *
 * The codepoint form above is what UTS #46 wants, because it has codepoints
 * already. Every other caller holds bytes, and sizing a buffer for the
 * codepoint form means knowing UAX #15's expansion bound - which belongs
 * here rather than in a parser.
 *
 * Always allocates, the empty string included, so the caller has one
 * ownership rule instead of a conditional one. Free `*out` through the same
 * allocator.
 *
 * @param alloc Allocator, or NULL for gtext_allocator_default().
 * @param in UTF-8 bytes. Not modified, and need not be NUL-terminated.
 * @param len How many bytes.
 * @param out Receives the NUL-terminated result. Set to NULL on any failure.
 * @param out_len Receives its length, excluding the terminator.
 * @return 1 on success; 0 if `in` is not well-formed UTF-8 or an argument was
 *         NULL; -1 if an allocation failed or the length cannot be
 *         represented. The two failures are distinguished because a caller
 *         reporting them has to tell "your input is wrong" from "I ran out of
 *         memory", and a single zero makes that a guess.
 */
GTEXT_INTERNAL_API int gtext_nfc_utf8(const GTEXT_Allocator * alloc,
    const char * in, size_t len, char ** out, size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_IDNA_NFC_UTF8_INTERNAL_H
