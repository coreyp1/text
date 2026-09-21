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
 * Internationalized host names: IDNA2008, RFC 5890 to 5893.
 *
 * This exists for JSON Schema's `hostname` and `idn-hostname` formats, which
 * are defined in terms of these RFCs. `hostname` is the reason the ASCII-only
 * path is not a special case: 2020-12 section 7.3.3 defines it as RFC 1123
 * section 2.1 *including* names produced by Punycode, so an `xn--` label in a
 * plain host name has to be decoded and checked like any other.
 */

#ifndef GHOTI_IO_GTEXT_SRC_IDNA_IDNA_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_IDNA_IDNA_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Is this a valid host name?
 *
 * @param text UTF-8, not NUL-terminated
 * @param len Bytes of input
 * @param allow_unicode 0 for `hostname`, which admits only ASCII labels and
 *   A-labels; 1 for `idn-hostname`, which also admits U-labels
 * @return 1 if valid, 0 if not
 */
GTEXT_INTERNAL_API int gtext_idna_hostname_valid(
    const char * text, size_t len, int allow_unicode);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_IDNA_IDNA_INTERNAL_H
