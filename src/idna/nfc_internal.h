/**
 * @file
 *
 * Unicode normalisation form C, internal to the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_SRC_IDNA_NFC_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_IDNA_NFC_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Normalise `len` codepoints to NFC.
 *
 * The output is written to `out`, which must have room for the *decomposed*
 * form as well: composition happens in place afterwards and only ever
 * shortens, but the intermediate needs the space. UAX #15 gives four
 * characters as the largest canonical expansion of one, so `4 * len` is
 * always enough and usually far more than needed.
 *
 * @param in Codepoints to normalise. Not modified.
 * @param len How many.
 * @param out Where to write. May not overlap `in`.
 * @param cap How many codepoints fit in `out`.
 * @param out_len Receives the length of the result.
 * @return 1 on success, 0 if `cap` was too small or an argument was NULL.
 */
GTEXT_INTERNAL_API int gtext_nfc(const uint32_t * in, size_t len,
    uint32_t * out, size_t cap, size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_IDNA_NFC_INTERNAL_H
