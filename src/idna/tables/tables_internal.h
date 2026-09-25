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
 * The shape of the generated IDNA tables.
 *
 * Hand-written; tables/idna_tables.c and tables/uts46_tables.c are
 * generated. The enumerations here are the values
 * those files name, so a table regenerated against newer data fails to
 * compile rather than silently meaning something else.
 */

#ifndef GHOTI_IO_GTEXT_SRC_IDNA_TABLES_TABLES_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_IDNA_TABLES_TABLES_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The IDNA2008 derived property, RFC 5892 section 1
 *
 * UNASSIGNED is absent on purpose: a codepoint the table does not mention is
 * unassigned, and for the only question asked here - may this appear in a
 * label - unassigned and disallowed are the same answer.
 */
typedef enum {
  GTEXT_IDNA_PVALID = 1,
  GTEXT_IDNA_CONTEXTJ = 2,
  GTEXT_IDNA_CONTEXTO = 3,
  GTEXT_IDNA_DISALLOWED = 4
} GTEXT_IDNA_Property;

/** One run of codepoints sharing a value. */
typedef struct {
  uint32_t lo;
  uint32_t hi;
  uint32_t value;
} GTEXT_IDNA_Range;

/* Script, Joining_Type, Bidi_Class and the viramas were four more tables here.
 * They were the UCD's data narrowed to the values RFC 5892 and RFC 5893 name;
 * ghoti.io-unicode answers all four now. What is left is RFC 5892's own
 * derived property, which is not a Unicode property and has no other home. */
extern const GTEXT_IDNA_Range gtext_idna_derived[];
extern const size_t gtext_idna_derived_count;

/**
 * @brief What UTS #46's mapping step does to a character
 *
 * Only two, because only two change the string. `valid` and `disallowed`
 * are absent because RFC 5892 decides validity here; `deviation` is absent
 * because nontransitional processing - which is what this library and every
 * current browser do - leaves all four deviations alone.
 */
typedef enum {
  GTEXT_UTS46_MAPPED = 0, ///< Replace with `length` codepoints from the pool
  GTEXT_UTS46_IGNORED = 1 ///< Remove
} GTEXT_UTS46_Status;

/** One character the mapping step changes. */
typedef struct {
  uint32_t cp;
  uint32_t offset; ///< Into gtext_uts46_pool
  uint8_t length;  ///< 0 when ignored
  uint8_t status;  ///< A GTEXT_UTS46_Status
} GTEXT_UTS46_Entry;

/**
 * Sorted by codepoint. A character that is not here is one the mapping step
 * leaves alone, which is the overwhelming majority of them.
 */
extern const GTEXT_UTS46_Entry gtext_uts46_map[];
extern const size_t gtext_uts46_map_count;
extern const uint32_t * const gtext_uts46_pool;

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_IDNA_TABLES_TABLES_INTERNAL_H
