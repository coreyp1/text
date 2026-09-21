/**
 * @file
 *
 * Unicode normalisation form C, for UTS #46's second step.
 *
 * UTS #46 section 4 maps a domain name and then normalises it, and its
 * validity criteria require a label to be in NFC. Neither is optional: `é`
 * written as `e` plus a combining acute is the same name as `é` written as one
 * character, and a validator that treated them as different names would be
 * the hole that spoofing walks through.
 *
 * The algorithm is UAX #15's, in three parts - decompose canonically, put
 * combining marks into canonical order, recompose - over the tables
 * tools/idna/gen_uts46.py derives from UnicodeData.txt. Only the canonical
 * mappings are here; the compatibility ones belong to NFKC, which nothing in
 * this library asks for.
 *
 * Hangul has no table. Its decomposition and composition are arithmetic, and
 * a table for eleven thousand syllables would be a table for something a
 * dozen lines compute.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stddef.h>
#include <stdint.h>

#include "nfc_internal.h"
#include "tables/tables_internal.h"

#define HANGUL_S_BASE 0xAC00u
#define HANGUL_L_BASE 0x1100u
#define HANGUL_V_BASE 0x1161u
#define HANGUL_T_BASE 0x11A7u
#define HANGUL_L_COUNT 19u
#define HANGUL_V_COUNT 21u
#define HANGUL_T_COUNT 28u
#define HANGUL_N_COUNT (HANGUL_V_COUNT * HANGUL_T_COUNT) // 588
#define HANGUL_S_COUNT (HANGUL_L_COUNT * HANGUL_N_COUNT) // 11172

/* Canonical_Combining_Class. Zero is the default, so the table holds only the
 * characters that have one, which is under a thousand of them. */
static uint8_t nfc_ccc(uint32_t cp) {
  size_t lo = 0;
  size_t hi = gtext_nfc_ccc_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < gtext_nfc_ccc[mid].lo) {
      hi = mid;
    }
    else if (cp > gtext_nfc_ccc[mid].hi) {
      lo = mid + 1;
    }
    else {
      return gtext_nfc_ccc[mid].value;
    }
  }
  return 0;
}

/* The canonical decomposition of `cp`, already fully expanded, or NULL. */
static const uint32_t * nfc_decomposition(uint32_t cp, size_t * out_len) {
  size_t lo = 0;
  size_t hi = gtext_nfc_decomposition_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < gtext_nfc_decomposition[mid].cp) {
      hi = mid;
    }
    else if (cp > gtext_nfc_decomposition[mid].cp) {
      lo = mid + 1;
    }
    else {
      *out_len = gtext_nfc_decomposition[mid].length;
      return gtext_nfc_decomposition_pool
          + gtext_nfc_decomposition[mid].offset;
    }
  }
  return NULL;
}

/* The character `first` and `second` canonically compose to, or 0. */
static uint32_t nfc_compose_pair(uint32_t first, uint32_t second) {
  /* Hangul first, because it is arithmetic and the table does not hold it. */
  if (first >= HANGUL_L_BASE && first < HANGUL_L_BASE + HANGUL_L_COUNT
      && second >= HANGUL_V_BASE && second < HANGUL_V_BASE + HANGUL_V_COUNT) {
    return HANGUL_S_BASE
        + ((first - HANGUL_L_BASE) * HANGUL_V_COUNT + (second - HANGUL_V_BASE))
            * HANGUL_T_COUNT;
  }
  if (first >= HANGUL_S_BASE && first < HANGUL_S_BASE + HANGUL_S_COUNT
      && (first - HANGUL_S_BASE) % HANGUL_T_COUNT == 0
      && second > HANGUL_T_BASE
      && second < HANGUL_T_BASE + HANGUL_T_COUNT) {
    return first + (second - HANGUL_T_BASE);
  }

  size_t lo = 0;
  size_t hi = gtext_nfc_composition_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const GTEXT_NFC_Composition * entry = &gtext_nfc_composition[mid];
    if (first < entry->first
        || (first == entry->first && second < entry->second)) {
      hi = mid;
    }
    else if (first > entry->first
        || (first == entry->first && second > entry->second)) {
      lo = mid + 1;
    }
    else {
      return entry->composite;
    }
  }
  return 0;
}

/* Append one character's canonical decomposition, or the character itself. */
static int nfc_decompose_one(
    uint32_t cp, uint32_t * out, size_t cap, size_t * used) {
  if (cp >= HANGUL_S_BASE && cp < HANGUL_S_BASE + HANGUL_S_COUNT) {
    uint32_t index = cp - HANGUL_S_BASE;
    uint32_t trailing = index % HANGUL_T_COUNT;
    size_t need = trailing ? 3 : 2;
    if (*used + need > cap) {
      return 0;
    }
    out[(*used)++] = HANGUL_L_BASE + index / HANGUL_N_COUNT;
    out[(*used)++] =
        HANGUL_V_BASE + (index % HANGUL_N_COUNT) / HANGUL_T_COUNT;
    if (trailing) {
      out[(*used)++] = HANGUL_T_BASE + trailing;
    }
    return 1;
  }

  size_t len = 0;
  const uint32_t * expansion = nfc_decomposition(cp, &len);
  if (!expansion) {
    if (*used + 1 > cap) {
      return 0;
    }
    out[(*used)++] = cp;
    return 1;
  }
  if (*used + len > cap) {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    out[(*used)++] = expansion[i];
  }
  return 1;
}

/*
 * Canonical ordering: an insertion sort over the combining marks, which is
 * what the algorithm calls for rather than a general sort. It must be stable,
 * because two marks of equal class are canonically equivalent in the order
 * they were written and swapping them would change the string.
 */
static void nfc_canonical_order(uint32_t * cps, size_t len) {
  for (size_t i = 1; i < len; i++) {
    uint8_t klass = nfc_ccc(cps[i]);
    if (klass == 0) {
      continue; // a starter never moves
    }
    uint32_t value = cps[i];
    size_t j = i;
    while (j > 0) {
      uint8_t before = nfc_ccc(cps[j - 1]);
      if (before == 0 || before <= klass) {
        break;
      }
      cps[j] = cps[j - 1];
      j--;
    }
    cps[j] = value;
  }
}

GTEXT_INTERNAL_API int gtext_nfc(const uint32_t * in, size_t len,
    uint32_t * out, size_t cap, size_t * out_len) {
  if (!in || !out || !out_len) {
    return 0;
  }
  *out_len = 0;
  if (len == 0) {
    return 1;
  }

  size_t used = 0;
  for (size_t i = 0; i < len; i++) {
    if (!nfc_decompose_one(in[i], out, cap, &used)) {
      return 0;
    }
  }
  nfc_canonical_order(out, used);

  /*
   * Canonical composition, UAX #15 section 16. `last_class` is the class of
   * the character immediately before the one being considered, and 256 when
   * the string began with a combining mark - nothing composes onto a starter
   * that is not there.
   *
   * The `last_class == 0` arm is the case a plain "the classes must
   * increase" test gets wrong: a character directly after a starter is never
   * blocked from it, which is how a Hangul L and V compose when both have
   * class zero.
   */
  size_t starter = 0;
  uint32_t starter_cp = out[0];
  size_t write = 1;
  unsigned int last_class = nfc_ccc(out[0]);
  if (last_class != 0) {
    last_class = 256;
  }
  for (size_t i = 1; i < used; i++) {
    uint32_t cp = out[i];
    unsigned int klass = nfc_ccc(cp);
    uint32_t composite = nfc_compose_pair(starter_cp, cp);
    if (composite != 0 && (last_class < klass || last_class == 0)) {
      out[starter] = composite;
      starter_cp = composite;
      continue;
    }
    if (klass == 0) {
      starter = write;
      starter_cp = cp;
    }
    last_class = klass;
    out[write++] = cp;
  }

  *out_len = write;
  return 1;
}
