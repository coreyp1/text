/**
 * @file
 *
 * Internationalized host names: IDNA2008, RFC 5890 to 5893.
 *
 * The shape of the thing: a host name is labels separated by dots; each label
 * is an A-label (`xn--` followed by Punycode), an ASCII LDH label, or a
 * U-label. An A-label is decoded and then checked exactly as a U-label would
 * be, because that is what it is - RFC 5891 section 4.4 says an A-label is
 * valid only if it round-trips to a valid U-label, so a validator that
 * accepts `xn--` and any trailing base-36 is not checking the thing the
 * keyword names.
 *
 * A U-label is checked against the derived property of RFC 5892 - PVALID,
 * DISALLOWED, or one of the two contextual classes - plus that appendix's
 * contextual rules and RFC 5893's bidi rule. The tables under tables/ are
 * generated from the UCD; everything here is the algorithm that reads them.
 *
 * Not implemented, and the reason it is written down here rather than left to
 * be discovered: UTS #46's mapping and normalisation step. A name is taken as
 * written, so `１２３` in fullwidth digits is not mapped to `123` and a label
 * that is not already in Normalization Form C is not put into it. Both are
 * refusals of something a browser would accept, which is the safe direction
 * to be wrong in but is still wrong. Doing it needs the UTS #46 mapping table
 * and a full NFC implementation, which is a larger piece of Unicode than
 * anything else here.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <string.h>

#include "idna_internal.h"
#include "tables/tables_internal.h"

#define IDNA_MAX_LABEL_CODEPOINTS 256
#define IDNA_MAX_NAME_OCTETS 253
#define IDNA_MAX_LABEL_OCTETS 63

// ===========================================================================
// Table lookup
// ===========================================================================

/* The ranges are sorted and disjoint, so this is a binary search; `absent` is
 * what a codepoint no range mentions means. */
static uint32_t idna_lookup(const GTEXT_IDNA_Range * ranges, size_t count,
    uint32_t cp, uint32_t absent) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < ranges[mid].lo) {
      hi = mid;
    }
    else if (cp > ranges[mid].hi) {
      lo = mid + 1;
    }
    else {
      return ranges[mid].value;
    }
  }
  return absent;
}

/* A codepoint no range mentions is unassigned, and for the only question
 * asked here that is the same answer as disallowed. */
static uint32_t idna_property(uint32_t cp) {
  return idna_lookup(gtext_idna_derived, gtext_idna_derived_count, cp,
      GTEXT_IDNA_DISALLOWED);
}

#define IDNA_SCRIPT_OTHER 0xFFFFu
#define IDNA_JOINING_OTHER 0xFFFFu
#define IDNA_BIDI_OTHER 0xFFFFu

static uint32_t idna_script(uint32_t cp) {
  return idna_lookup(
      gtext_idna_script, gtext_idna_script_count, cp, IDNA_SCRIPT_OTHER);
}

static uint32_t idna_joining(uint32_t cp) {
  return idna_lookup(
      gtext_idna_joining, gtext_idna_joining_count, cp, IDNA_JOINING_OTHER);
}

static uint32_t idna_bidi(uint32_t cp) {
  return idna_lookup(
      gtext_idna_bidi, gtext_idna_bidi_count, cp, IDNA_BIDI_OTHER);
}

static int idna_is_virama(uint32_t cp) {
  return idna_lookup(gtext_idna_virama, gtext_idna_virama_count, cp, 1) == 0;
}

// ===========================================================================
// UTF-8
// ===========================================================================

/*
 * Decode one character, or return 0 and leave `pos` alone.
 *
 * Strict: an overlong form, a surrogate and a value past U+10FFFF are all
 * refused rather than repaired, because a host name that needs repairing is
 * not a host name.
 */
static uint32_t idna_utf8_next(const char * s, size_t len, size_t * pos) {
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
  if (*pos + extra >= len + 0 && *pos + extra > len - 1) {
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

// ===========================================================================
// Punycode, RFC 3492
// ===========================================================================

#define PUNY_BASE 36
#define PUNY_TMIN 1
#define PUNY_TMAX 26
#define PUNY_SKEW 38
#define PUNY_DAMP 700
#define PUNY_INITIAL_BIAS 72
#define PUNY_INITIAL_N 128

static uint32_t puny_adapt(uint32_t delta, uint32_t numpoints, int firsttime) {
  uint32_t k = 0;
  delta = firsttime ? delta / PUNY_DAMP : delta / 2;
  delta += delta / numpoints;
  while (delta > ((PUNY_BASE - PUNY_TMIN) * PUNY_TMAX) / 2) {
    delta /= PUNY_BASE - PUNY_TMIN;
    k += PUNY_BASE;
  }
  return k + (PUNY_BASE - PUNY_TMIN + 1) * delta / (delta + PUNY_SKEW);
}

static int puny_digit(unsigned char c, uint32_t * out) {
  if (c >= 'a' && c <= 'z') {
    *out = c - 'a';
    return 1;
  }
  if (c >= 'A' && c <= 'Z') {
    *out = c - 'A';
    return 1;
  }
  if (c >= '0' && c <= '9') {
    *out = c - '0' + 26;
    return 1;
  }
  return 0;
}

static char puny_char(uint32_t d) {
  return (char)(d < 26 ? 'a' + d : '0' + (d - 26));
}

/* Decode the part after "xn--". Returns 1 on success. */
static int puny_decode(
    const char * in, size_t len, uint32_t * out, size_t * out_len) {
  uint32_t n = PUNY_INITIAL_N;
  uint32_t i = 0;
  uint32_t bias = PUNY_INITIAL_BIAS;
  size_t written = 0;
  size_t start = 0;

  /* Everything before the last delimiter is literal ASCII. */
  for (size_t k = 0; k < len; k++) {
    if (in[k] == '-') {
      start = k + 1;
    }
  }
  for (size_t k = 0; k + 1 <= start && k < (start ? start - 1 : 0); k++) {
    unsigned char c = (unsigned char)in[k];
    if (c >= 0x80) {
      return 0;
    }
    if (written >= IDNA_MAX_LABEL_CODEPOINTS) {
      return 0;
    }
    out[written++] = c;
  }

  size_t at = start;
  while (at < len) {
    uint32_t oldi = i;
    uint32_t w = 1;
    for (uint32_t k = PUNY_BASE;; k += PUNY_BASE) {
      if (at >= len) {
        return 0;
      }
      uint32_t digit;
      if (!puny_digit((unsigned char)in[at++], &digit)) {
        return 0;
      }
      if (digit > (0xFFFFFFFFu - i) / w) {
        return 0; // overflow
      }
      i += digit * w;
      uint32_t t = k <= bias ? PUNY_TMIN
          : (k >= bias + PUNY_TMAX ? PUNY_TMAX : k - bias);
      if (digit < t) {
        break;
      }
      if (w > 0xFFFFFFFFu / (PUNY_BASE - t)) {
        return 0;
      }
      w *= PUNY_BASE - t;
    }
    bias = puny_adapt(i - oldi, (uint32_t)written + 1, oldi == 0);
    if (i / (written + 1) > 0xFFFFFFFFu - n) {
      return 0;
    }
    n += i / (uint32_t)(written + 1);
    i %= (uint32_t)(written + 1);
    if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF)) {
      return 0;
    }
    if (written >= IDNA_MAX_LABEL_CODEPOINTS) {
      return 0;
    }
    memmove(out + i + 1, out + i, (written - i) * sizeof(*out));
    out[i] = n;
    written++;
    i++;
  }
  *out_len = written;
  return 1;
}

/*
 * Encode, so that a decoded A-label can be checked against the label it came
 * from. RFC 5891 section 4.4 requires the round trip: `xn---9uc` decodes to
 * something whose own encoding is different, and a decoder alone would take
 * it. The encoder is also what measures a U-label's A-label form against the
 * 63-octet limit, which is a limit on the encoded form and not on the text.
 */
static int puny_encode(const uint32_t * in, size_t len, char * out,
    size_t capacity, size_t * out_len) {
  uint32_t n = PUNY_INITIAL_N;
  uint32_t delta = 0;
  uint32_t bias = PUNY_INITIAL_BIAS;
  size_t written = 0;
  size_t handled = 0;

  for (size_t k = 0; k < len; k++) {
    if (in[k] < 0x80) {
      if (written >= capacity) {
        return 0;
      }
      out[written++] = (char)in[k];
      handled++;
    }
  }
  size_t basic = handled;
  if (basic > 0) {
    if (written >= capacity) {
      return 0;
    }
    out[written++] = '-';
  }

  while (handled < len) {
    uint32_t m = 0xFFFFFFFFu;
    for (size_t k = 0; k < len; k++) {
      if (in[k] >= n && in[k] < m) {
        m = in[k];
      }
    }
    if (m - n > (0xFFFFFFFFu - delta) / (uint32_t)(handled + 1)) {
      return 0;
    }
    delta += (m - n) * (uint32_t)(handled + 1);
    n = m;
    for (size_t k = 0; k < len; k++) {
      if (in[k] < n) {
        if (++delta == 0) {
          return 0;
        }
        continue;
      }
      if (in[k] != n) {
        continue;
      }
      uint32_t q = delta;
      for (uint32_t k2 = PUNY_BASE;; k2 += PUNY_BASE) {
        uint32_t t = k2 <= bias ? PUNY_TMIN
            : (k2 >= bias + PUNY_TMAX ? PUNY_TMAX : k2 - bias);
        if (q < t) {
          break;
        }
        if (written >= capacity) {
          return 0;
        }
        out[written++] = puny_char(t + (q - t) % (PUNY_BASE - t));
        q = (q - t) / (PUNY_BASE - t);
      }
      if (written >= capacity) {
        return 0;
      }
      out[written++] = puny_char(q);
      bias = puny_adapt(delta, (uint32_t)(handled + 1), handled == basic);
      delta = 0;
      handled++;
    }
    delta++;
    n++;
  }
  *out_len = written;
  return 1;
}

// ===========================================================================
// RFC 5892 appendix A, the contextual rules
// ===========================================================================

static int idna_contextj(const uint32_t * cps, size_t len, size_t at) {
  uint32_t cp = cps[at];
  if (cp == 0x200D) { // ZERO WIDTH JOINER, rule A.2
    return at > 0 && idna_is_virama(cps[at - 1]);
  }
  if (cp != 0x200C) {
    return 0;
  }
  // ZERO WIDTH NON-JOINER, rule A.1: a virama before it, or the joining-type
  // pattern (L|D) T* ZWNJ T* (R|D) around it.
  if (at > 0 && idna_is_virama(cps[at - 1])) {
    return 1;
  }
  size_t i = at;
  while (i > 0 && idna_joining(cps[i - 1]) == GTEXT_IDNA_JOINING_T) {
    i--;
  }
  if (i == 0) {
    return 0;
  }
  uint32_t before = idna_joining(cps[i - 1]);
  if (before != GTEXT_IDNA_JOINING_L && before != GTEXT_IDNA_JOINING_D) {
    return 0;
  }
  size_t j = at + 1;
  while (j < len && idna_joining(cps[j]) == GTEXT_IDNA_JOINING_T) {
    j++;
  }
  if (j >= len) {
    return 0;
  }
  uint32_t after = idna_joining(cps[j]);
  return after == GTEXT_IDNA_JOINING_R || after == GTEXT_IDNA_JOINING_D;
}

static int idna_contexto(const uint32_t * cps, size_t len, size_t at) {
  uint32_t cp = cps[at];
  switch (cp) {
  case 0x00B7: // MIDDLE DOT, rule A.3
    return at > 0 && at + 1 < len && cps[at - 1] == 0x006C
        && cps[at + 1] == 0x006C;
  case 0x0375: // GREEK LOWER NUMERAL SIGN, rule A.4
    return at + 1 < len && idna_script(cps[at + 1]) == GTEXT_IDNA_SCRIPT_GREEK;
  case 0x05F3: // HEBREW PUNCTUATION GERESH, rule A.5
  case 0x05F4: // HEBREW PUNCTUATION GERSHAYIM, rule A.6
    return at > 0 && idna_script(cps[at - 1]) == GTEXT_IDNA_SCRIPT_HEBREW;
  case 0x30FB: { // KATAKANA MIDDLE DOT, rule A.7
    for (size_t i = 0; i < len; i++) {
      uint32_t script = idna_script(cps[i]);
      if (script == GTEXT_IDNA_SCRIPT_HIRAGANA
          || script == GTEXT_IDNA_SCRIPT_KATAKANA
          || script == GTEXT_IDNA_SCRIPT_HAN) {
        return 1;
      }
    }
    return 0;
  }
  default:
    break;
  }
  if (cp >= 0x0660 && cp <= 0x0669) { // ARABIC-INDIC DIGITS, rule A.8
    for (size_t i = 0; i < len; i++) {
      if (cps[i] >= 0x06F0 && cps[i] <= 0x06F9) {
        return 0;
      }
    }
    return 1;
  }
  if (cp >= 0x06F0 && cp <= 0x06F9) { // EXTENDED ARABIC-INDIC, rule A.9
    for (size_t i = 0; i < len; i++) {
      if (cps[i] >= 0x0660 && cps[i] <= 0x0669) {
        return 0;
      }
    }
    return 1;
  }
  return 0;
}

// ===========================================================================
// RFC 5893, the bidi rule
// ===========================================================================

static int idna_label_has_rtl(const uint32_t * cps, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint32_t class = idna_bidi(cps[i]);
    if (class == GTEXT_IDNA_BIDI_R || class == GTEXT_IDNA_BIDI_AL
        || class == GTEXT_IDNA_BIDI_AN) {
      return 1;
    }
  }
  return 0;
}

/*
 * RFC 5893 section 2, applied to every label once any label in the name has
 * shown the name to be a bidi domain name. The direction of a label is the
 * class of its first character, which is why "0a.א" fails: the name is a bidi
 * domain name because of the second label, and the first label then starts
 * with a digit, which is neither L nor R nor AL.
 */
static int idna_bidi_label_valid(const uint32_t * cps, size_t len) {
  if (len == 0) {
    return 0;
  }
  uint32_t first = idna_bidi(cps[0]);
  size_t last = len;
  while (last > 0 && idna_bidi(cps[last - 1]) == GTEXT_IDNA_BIDI_NSM) {
    last--; // trailing combining marks do not decide the ending class
  }
  if (last == 0) {
    return 0;
  }
  uint32_t ending = idna_bidi(cps[last - 1]);

  if (first == GTEXT_IDNA_BIDI_R || first == GTEXT_IDNA_BIDI_AL) {
    int has_en = 0;
    int has_an = 0;
    for (size_t i = 0; i < len; i++) {
      switch (idna_bidi(cps[i])) {
      case GTEXT_IDNA_BIDI_R:
      case GTEXT_IDNA_BIDI_AL:
      case GTEXT_IDNA_BIDI_ES:
      case GTEXT_IDNA_BIDI_CS:
      case GTEXT_IDNA_BIDI_ET:
      case GTEXT_IDNA_BIDI_ON:
      case GTEXT_IDNA_BIDI_BN:
      case GTEXT_IDNA_BIDI_NSM:
        break;
      case GTEXT_IDNA_BIDI_EN:
        has_en = 1;
        break;
      case GTEXT_IDNA_BIDI_AN:
        has_an = 1;
        break;
      default:
        return 0;
      }
    }
    if (has_en && has_an) {
      return 0; // rule 4: not both kinds of digit
    }
    return ending == GTEXT_IDNA_BIDI_R || ending == GTEXT_IDNA_BIDI_AL
        || ending == GTEXT_IDNA_BIDI_EN || ending == GTEXT_IDNA_BIDI_AN;
  }
  if (first == GTEXT_IDNA_BIDI_L) {
    for (size_t i = 0; i < len; i++) {
      switch (idna_bidi(cps[i])) {
      case GTEXT_IDNA_BIDI_L:
      case GTEXT_IDNA_BIDI_EN:
      case GTEXT_IDNA_BIDI_ES:
      case GTEXT_IDNA_BIDI_CS:
      case GTEXT_IDNA_BIDI_ET:
      case GTEXT_IDNA_BIDI_ON:
      case GTEXT_IDNA_BIDI_BN:
      case GTEXT_IDNA_BIDI_NSM:
        break;
      default:
        return 0;
      }
    }
    return ending == GTEXT_IDNA_BIDI_L || ending == GTEXT_IDNA_BIDI_EN;
  }
  return 0; // rule 1: a label starts L, R or AL and nothing else
}

// ===========================================================================
// Labels
// ===========================================================================

/* Mn, Mc and Me, which a label may not start with (RFC 5891 section 4.2.3.2).
 * The derived table does not carry General_Category, but every combining mark
 * that is PVALID is one of these, and the rule only bites on a mark that
 * would otherwise be allowed - so the question is asked of the one property
 * that is here: a leading character whose bidi class is NSM, or whose
 * canonical combining class is non-zero. */
static int idna_is_leading_mark(uint32_t cp) {
  return idna_bidi(cp) == GTEXT_IDNA_BIDI_NSM || idna_is_virama(cp)
      || (cp >= 0x0900 && cp <= 0x0903) || (cp >= 0x093A && cp <= 0x093C)
      || (cp >= 0x093E && cp <= 0x094F) || (cp >= 0x0951 && cp <= 0x0957);
}

/*
 * A U-label, as codepoints. `bidi_domain` says whether any label in the name
 * carried a right-to-left character, which is a property of the whole name
 * and so cannot be decided here.
 */
static int idna_u_label_valid(
    const uint32_t * cps, size_t len, int bidi_domain) {
  if (len == 0) {
    return 0;
  }
  /* RFC 5891 section 4.2.3.1: no leading or trailing hyphen, and not a
   * hyphen in both the third and fourth positions - that pattern is reserved
   * for the ACE prefix and labels like `aa--bb` are not names. */
  if (cps[0] == '-' || cps[len - 1] == '-') {
    return 0;
  }
  if (len >= 4 && cps[2] == '-' && cps[3] == '-') {
    return 0;
  }
  if (idna_is_leading_mark(cps[0])) {
    return 0;
  }

  for (size_t i = 0; i < len; i++) {
    switch (idna_property(cps[i])) {
    case GTEXT_IDNA_PVALID:
      break;
    case GTEXT_IDNA_CONTEXTJ:
      if (!idna_contextj(cps, len, i)) {
        return 0;
      }
      break;
    case GTEXT_IDNA_CONTEXTO:
      if (!idna_contexto(cps, len, i)) {
        return 0;
      }
      break;
    default:
      return 0;
    }
  }
  if (bidi_domain && !idna_bidi_label_valid(cps, len)) {
    return 0;
  }
  return 1;
}

/* An ASCII LDH label: letters, digits and hyphens, not starting or ending
 * with a hyphen. */
static int idna_ldh_label_valid(const char * s, size_t len) {
  if (len == 0 || len > IDNA_MAX_LABEL_OCTETS) {
    return 0;
  }
  if (s[0] == '-' || s[len - 1] == '-') {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '-') {
      continue;
    }
    return 0;
  }
  return 1;
}

static int idna_has_ace_prefix(const char * s, size_t len) {
  return len >= 4 && (s[0] == 'x' || s[0] == 'X')
      && (s[1] == 'n' || s[1] == 'N') && s[2] == '-' && s[3] == '-';
}

/*
 * Decode one label to codepoints. An A-label is decoded from Punycode and
 * required to re-encode to itself; anything else is read as UTF-8.
 */
static int idna_label_codepoints(const char * s, size_t len, uint32_t * cps,
    size_t * out_len, int * was_a_label) {
  *was_a_label = 0;
  if (idna_has_ace_prefix(s, len)) {
    char lower[IDNA_MAX_LABEL_OCTETS + 1];
    if (len > IDNA_MAX_LABEL_OCTETS) {
      return 0;
    }
    for (size_t i = 0; i < len; i++) {
      char c = s[i];
      lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    if (!idna_ldh_label_valid(s, len)) {
      return 0;
    }
    if (!puny_decode(lower + 4, len - 4, cps, out_len)) {
      return 0;
    }
    if (*out_len == 0) {
      return 0;
    }
    /* RFC 5891 section 4.4: the decoded label must encode back to exactly
     * what was written, which is what refuses a non-canonical encoding. And
     * a label that decodes to pure ASCII was never an A-label at all. */
    char again[IDNA_MAX_LABEL_OCTETS * 2];
    size_t again_len = 0;
    if (!puny_encode(cps, *out_len, again, sizeof(again), &again_len)) {
      return 0;
    }
    if (again_len != len - 4 || memcmp(again, lower + 4, again_len) != 0) {
      return 0;
    }
    int all_ascii = 1;
    for (size_t i = 0; i < *out_len; i++) {
      if (cps[i] >= 0x80) {
        all_ascii = 0;
        break;
      }
    }
    if (all_ascii) {
      return 0;
    }
    *was_a_label = 1;
    return 1;
  }

  size_t at = 0;
  size_t written = 0;
  while (at < len) {
    size_t before = at;
    uint32_t cp = idna_utf8_next(s, len, &at);
    if (cp == 0 && at == before) {
      return 0;
    }
    if (written >= IDNA_MAX_LABEL_CODEPOINTS) {
      return 0;
    }
    cps[written++] = cp;
  }
  *out_len = written;
  return 1;
}

// ===========================================================================
// Host names
// ===========================================================================

/*
 * UTS #46 section 4.5 treats three other stops as label separators, and the
 * suite requires it: `a。b` is two labels. This much of UTS #46 is three
 * codepoints and no table, which is why it is here when the rest of that
 * specification's mapping step is not.
 */
static int idna_is_separator(uint32_t cp, int allow_unicode) {
  if (cp == '.') {
    return 1;
  }
  /* Only for `idn-hostname`. A plain `hostname` is ASCII, so `example．com`
   * is one label containing a character no label may contain rather than two
   * labels either side of a separator - and it is invalid either way, but
   * for the reason that is actually true of it. */
  return allow_unicode && (cp == 0x3002 || cp == 0xFF0E || cp == 0xFF61);
}

GTEXT_INTERNAL_API int gtext_idna_hostname_valid(
    const char * text, size_t len, int allow_unicode) {
  if (len == 0 || len > 4 * IDNA_MAX_NAME_OCTETS) {
    return 0;
  }

  /* Split into labels first, because the bidi rule is a property of the whole
   * name: a label that would pass on its own fails once some other label has
   * made this a bidi domain name. */
  size_t starts[IDNA_MAX_NAME_OCTETS + 1];
  size_t lengths[IDNA_MAX_NAME_OCTETS + 1];
  size_t count = 0;
  size_t at = 0;
  size_t label_start = 0;
  while (at < len) {
    size_t before = at;
    uint32_t cp = idna_utf8_next(text, len, &at);
    if (cp == 0 && at == before) {
      return 0;
    }
    if (!idna_is_separator(cp, allow_unicode)) {
      continue;
    }
    if (count >= IDNA_MAX_NAME_OCTETS) {
      return 0;
    }
    starts[count] = label_start;
    lengths[count] = before - label_start;
    count++;
    label_start = at;
  }
  if (count >= IDNA_MAX_NAME_OCTETS) {
    return 0;
  }
  starts[count] = label_start;
  lengths[count] = len - label_start;
  count++;

  /* Every label is decoded once, then checked; the two passes are what lets
   * the bidi question be answered before any label is judged. */
  uint32_t buffer[IDNA_MAX_LABEL_CODEPOINTS];
  size_t total_octets = 0;
  int bidi_domain = 0;

  for (size_t pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < count; i++) {
      const char * label = text + starts[i];
      size_t label_len = lengths[i];
      if (label_len == 0) {
        return 0; // an empty label, which covers a leading or trailing stop
      }
      size_t n = 0;
      int was_a_label = 0;
      if (!idna_label_codepoints(label, label_len, buffer, &n, &was_a_label)) {
        return 0;
      }
      int ascii_only = 1;
      for (size_t k = 0; k < n; k++) {
        if (buffer[k] >= 0x80) {
          ascii_only = 0;
          break;
        }
      }

      if (pass == 0) {
        if (!was_a_label && !ascii_only && !allow_unicode) {
          return 0; // a U-label where only ASCII is allowed
        }
        if (!ascii_only && idna_label_has_rtl(buffer, n)) {
          bidi_domain = 1;
        }
        continue;
      }

      if (was_a_label || !ascii_only) {
        if (!idna_u_label_valid(buffer, n, bidi_domain)) {
          return 0;
        }
        /* The 63-octet limit is on the A-label form, so a U-label is
         * measured by what it would encode to and not by its own length. */
        char encoded[IDNA_MAX_LABEL_OCTETS * 4];
        size_t encoded_len = 0;
        if (!puny_encode(buffer, n, encoded, sizeof(encoded), &encoded_len)) {
          return 0;
        }
        if (encoded_len + 4 > IDNA_MAX_LABEL_OCTETS) {
          return 0;
        }
        total_octets += encoded_len + 4;
      }
      else {
        if (!idna_ldh_label_valid(label, label_len)) {
          return 0;
        }
        if (bidi_domain && !idna_bidi_label_valid(buffer, n)) {
          return 0;
        }
        total_octets += label_len;
      }
      total_octets += 1; // the separator, or the root label's own octet
    }
  }
  return total_octets - 1 <= IDNA_MAX_NAME_OCTETS;
}
