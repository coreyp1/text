/**
 * @file
 *
 * The shape of the generated IDNA tables.
 *
 * Hand-written; tables/idna_tables.c, tables/uts46_tables.c and
 * tables/nfc_tables.c are generated. The enumerations here are the values
 * those files name, so a table regenerated against newer data fails to
 * compile rather than silently meaning something else.
 *
 * Copyright 2026 by Corey Pennycuff
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

/** The scripts the CONTEXTO rules name, and no others. */
typedef enum {
  GTEXT_IDNA_SCRIPT_GREEK = 0,
  GTEXT_IDNA_SCRIPT_HEBREW = 1,
  GTEXT_IDNA_SCRIPT_HIRAGANA = 2,
  GTEXT_IDNA_SCRIPT_KATAKANA = 3,
  GTEXT_IDNA_SCRIPT_HAN = 4
} GTEXT_IDNA_Script;

/** The Joining_Type values the zero-width-non-joiner rule reads. */
typedef enum {
  GTEXT_IDNA_JOINING_T = 0,
  GTEXT_IDNA_JOINING_L = 1,
  GTEXT_IDNA_JOINING_R = 2,
  GTEXT_IDNA_JOINING_D = 3
} GTEXT_IDNA_Joining;

/** The Bidi_Class values RFC 5893's rule distinguishes. */
typedef enum {
  GTEXT_IDNA_BIDI_L = 0,
  GTEXT_IDNA_BIDI_R = 1,
  GTEXT_IDNA_BIDI_AL = 2,
  GTEXT_IDNA_BIDI_AN = 3,
  GTEXT_IDNA_BIDI_EN = 4,
  GTEXT_IDNA_BIDI_ES = 5,
  GTEXT_IDNA_BIDI_CS = 6,
  GTEXT_IDNA_BIDI_ET = 7,
  GTEXT_IDNA_BIDI_ON = 8,
  GTEXT_IDNA_BIDI_BN = 9,
  GTEXT_IDNA_BIDI_NSM = 10
} GTEXT_IDNA_Bidi;

/** One run of codepoints sharing a value. */
typedef struct {
  uint32_t lo;
  uint32_t hi;
  uint32_t value;
} GTEXT_IDNA_Range;

extern const GTEXT_IDNA_Range gtext_idna_derived[];
extern const size_t gtext_idna_derived_count;
extern const GTEXT_IDNA_Range gtext_idna_script[];
extern const size_t gtext_idna_script_count;
extern const GTEXT_IDNA_Range gtext_idna_joining[];
extern const size_t gtext_idna_joining_count;
extern const GTEXT_IDNA_Range gtext_idna_bidi[];
extern const size_t gtext_idna_bidi_count;
extern const GTEXT_IDNA_Range gtext_idna_virama[];
extern const size_t gtext_idna_virama_count;

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

/** A run of codepoints sharing a combining class. */
typedef struct {
  uint32_t lo;
  uint32_t hi;
  uint8_t value;
} GTEXT_NFC_Range;

/**
 * One canonical decomposition, already expanded as far as it goes, so that
 * the runtime never has to decompose a decomposition.
 */
typedef struct {
  uint32_t cp;
  uint32_t offset; ///< Into gtext_nfc_decomposition_pool
  uint8_t length;  ///< At most 4, which UAX #15 gives as the maximum
} GTEXT_NFC_Decomposition;

/** A starter and a following character that canonically compose. */
typedef struct {
  uint32_t first;
  uint32_t second;
  uint32_t composite;
} GTEXT_NFC_Composition;

/** Canonical_Combining_Class, zero being the default and so absent. */
extern const GTEXT_NFC_Range gtext_nfc_ccc[];
extern const size_t gtext_nfc_ccc_count;

/** Sorted by codepoint. */
extern const GTEXT_NFC_Decomposition gtext_nfc_decomposition[];
extern const size_t gtext_nfc_decomposition_count;
extern const uint32_t * const gtext_nfc_decomposition_pool;

/** Sorted by (first, second). */
extern const GTEXT_NFC_Composition gtext_nfc_composition[];
extern const size_t gtext_nfc_composition_count;

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_IDNA_TABLES_TABLES_INTERNAL_H
