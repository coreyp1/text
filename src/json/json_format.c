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
 * The `format` keyword's assertions.
 *
 * `format` is an annotation in 2020-12: an implementation collects it and
 * asserts nothing, and one that refuses a schema for carrying it - as this
 * one did - is not conformant. So the default here is to ignore it, and a
 * caller who wants it checked asks for that with GTEXT_JSON_FORMAT_ASSERT.
 *
 * Under that policy a name in the format-annotation vocabulary that this
 * library cannot check is refused at compile time rather than ignored. The
 * caller asked for the constraint; handing back a schema that does not carry
 * it, and no way to find that out, is the failure the whole strict-keyword
 * check exists to prevent. A name *outside* the vocabulary - a vendor's own
 * `"format": "phone-number"` - is ignored, because the specification requires
 * that and because nothing was promised about it.
 *
 * Two of these are somebody else's problem on purpose. `date`, `date-time`,
 * `time` and `duration` are ghoti.io-chron's grammars, which are tested
 * against this same suite in that library; `regex` is the caller's
 * regular-expression provider. Writing either here would mean a second, worse
 * implementation of something already present and already measured.
 */

#include <ghoti.io/chron/chron.h>
#include <stdbool.h>
#include <string.h>

#include "../idna/idna_internal.h"
#include "json_internal.h"

/*
 * The names 2020-12's format-annotation vocabulary defines. A name in this
 * list is one a caller can reasonably expect to be checked; a name outside it
 * is nobody's to check.
 */
static const char * const json_format_vocabulary[] = {"date-time", "date",
    "time", "duration", "email", "idn-email", "hostname", "idn-hostname",
    "ipv4", "ipv6", "uri", "uri-reference", "iri", "iri-reference",
    "uuid", "uri-template", "json-pointer", "relative-json-pointer", "regex",
    NULL};

static bool json_format_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static bool json_format_is_hex(char c) {
  return json_format_is_digit(c) || (c >= 'a' && c <= 'f')
      || (c >= 'A' && c <= 'F');
}

static bool json_format_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* RFC 3986 unreserved / sub-delims, which several of the URI rules share. */
static bool json_format_is_unreserved(char c) {
  return json_format_is_alpha(c) || json_format_is_digit(c) || c == '-'
      || c == '.' || c == '_' || c == '~';
}

static bool json_format_is_subdelim(char c) {
  return c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' || c == ')'
      || c == '*' || c == '+' || c == ',' || c == ';' || c == '=';
}

/*
 * RFC 1123 section 2.1: labels of letters, digits and hyphens, no label
 * empty, none longer than 63, none starting or ending with a hyphen. The
 * rooted form with a trailing dot is not accepted, which is the reading the
 * suite takes - the empty root label is an empty label.
 *
 * A label beginning "xn--" is an A-label, and checking one properly means
 * decoding the punycode and applying IDNA2008 to what comes out. That needs
 * Unicode tables this library does not carry, so such a label is checked as
 * the LDH label it also is and no further. It is the one place where this
 * function is weaker than the name suggests, and it is said here rather than
 * left to be discovered.
 */
static bool json_format_hostname(const char * s, size_t len) {
  if (len == 0 || len > 253) {
    return false;
  }
  size_t label = 0;
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    if (c == '.') {
      if (label == 0 || s[i - 1] == '-') {
        return false;
      }
      label = 0;
      continue;
    }
    if (!json_format_is_alpha(c) && !json_format_is_digit(c) && c != '-') {
      return false;
    }
    if (label == 0 && c == '-') {
      return false;
    }
    if (++label > 63) {
      return false;
    }
  }
  return label != 0 && s[len - 1] != '-';
}

/* Four decimal octets, no leading zeros - "087.10.0.1" is not an address. */
static bool json_format_ipv4(const char * s, size_t len) {
  int octets = 0;
  size_t i = 0;
  while (i < len) {
    if (!json_format_is_digit(s[i])) {
      return false;
    }
    size_t start = i;
    unsigned value = 0;
    while (i < len && json_format_is_digit(s[i])) {
      value = value * 10 + (unsigned)(s[i] - '0');
      if (value > 255 || i - start > 2) {
        return false;
      }
      i++;
    }
    if (i - start > 1 && s[start] == '0') {
      return false;
    }
    octets++;
    if (i == len) {
      break;
    }
    if (s[i] != '.') {
      return false;
    }
    i++;
    if (i == len) {
      return false; // a trailing dot
    }
  }
  return octets == 4;
}

/*
 * One run of colon-separated groups, as the half of an address on either side
 * of a "::" elision - or the whole of one when there is none.
 *
 * Returns the number of 16-bit groups it accounts for, or -1 if it is not a
 * run of groups.  A trailing dotted quad stands for the last two, and may
 * only appear at the end, which is why `allow_v4_tail` is not always set.
 */
static int json_format_ipv6_run(
    const char * s, size_t len, bool allow_v4_tail) {
  if (len == 0) {
    return 0;
  }
  int groups = 0;
  size_t i = 0;
  for (;;) {
    size_t start = i;
    while (i < len && s[i] != ':') {
      i++;
    }
    size_t piece = i - start;
    if (piece == 0) {
      return -1; // an empty group: a doubled or dangling colon
    }
    if (memchr(s + start, '.', piece) != NULL) {
      /* A dotted quad takes the place of the last two groups, so nothing may
       * follow it. */
      if (!allow_v4_tail || i != len
          || !json_format_ipv4(s + start, piece)) {
        return -1;
      }
      groups += 2;
      return groups;
    }
    if (piece > 4) {
      return -1;
    }
    for (size_t k = start; k < i; k++) {
      if (!json_format_is_hex(s[k])) {
        return -1;
      }
    }
    groups++;
    if (i == len) {
      return groups;
    }
    i++; // the separating colon
    if (i == len) {
      return -1; // a dangling colon
    }
  }
}

/*
 * RFC 4291 section 2.2.  The "::" elision may appear once and stands for at
 * least one group of zeros; everything else is eight groups.
 *
 * Split at the elision rather than walked character by character, because the
 * walking version accepted `1:2:3:4:5:::8` - it consumed the third colon as a
 * separator and never noticed the group that was missing between them.
 */
static bool json_format_ipv6(const char * s, size_t len) {
  const char * elision = NULL;
  for (size_t i = 0; i + 1 < len; i++) {
    if (s[i] == ':' && s[i + 1] == ':') {
      if (elision) {
        return false; // "::" twice
      }
      elision = s + i;
      i++;
    }
  }

  if (!elision) {
    return json_format_ipv6_run(s, len, true) == 8;
  }
  size_t left_len = (size_t)(elision - s);
  const char * right = elision + 2;
  size_t right_len = len - left_len - 2;
  int left = json_format_ipv6_run(s, left_len, false);
  int rights = json_format_ipv6_run(right, right_len, true);
  if (left < 0 || rights < 0) {
    return false;
  }
  /* The elision must stand for at least one group, so the two halves cannot
   * already account for all eight. */
  return left + rights < 8;
}

/* 8-4-4-4-12 hexadecimal digits. */
static bool json_format_uuid(const char * s, size_t len) {
  static const size_t runs[] = {8, 4, 4, 4, 12};
  if (len != 36) {
    return false;
  }
  size_t i = 0;
  for (size_t run = 0; run < 5; run++) {
    for (size_t k = 0; k < runs[run]; k++) {
      if (!json_format_is_hex(s[i++])) {
        return false;
      }
    }
    if (run < 4 && s[i++] != '-') {
      return false;
    }
  }
  return true;
}

/*
 * RFC 6901: a pointer is empty, or a sequence of "/" segments in which "~"
 * appears only as "~0" or "~1".
 */
static bool json_format_json_pointer(const char * s, size_t len) {
  if (len == 0) {
    return true;
  }
  if (s[0] != '/') {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    if (s[i] != '~') {
      continue;
    }
    if (i + 1 >= len || (s[i + 1] != '0' && s[i + 1] != '1')) {
      return false;
    }
    i++;
  }
  return true;
}

/*
 * RFC 6901 section 4: a non-negative integer with no leading zeros, then
 * either "#" or a JSON Pointer.
 */
static bool json_format_relative_json_pointer(const char * s, size_t len) {
  size_t i = 0;
  while (i < len && json_format_is_digit(s[i])) {
    i++;
  }
  if (i == 0) {
    return false;
  }
  if (i > 1 && s[0] == '0') {
    return false;
  }
  if (i == len) {
    return true;
  }
  if (s[i] == '#') {
    return i + 1 == len;
  }
  return json_format_json_pointer(s + i, len - i);
}

/*
 * RFC 5322 section 3.4.1, in the shape everything actually uses: a dot-atom
 * or a quoted string, an "@", then a hostname or an IP literal in brackets.
 * `allow_unicode` is the difference between `email` and `idn-email`.
 */
static bool json_format_email(const char * s, size_t len, bool allow_unicode) {
  const char * at = NULL;
  size_t i = 0;

  if (len == 0) {
    return false;
  }
  /* A quoted local part may contain the "@" that would otherwise split it. */
  if (s[0] == '"') {
    for (i = 1; i < len; i++) {
      if (s[i] == '\\' && i + 1 < len) {
        /* A quoted pair is "\\" then a VCHAR or a space, and a non-ASCII byte
         * is neither - which is the difference between `email` and
         * `idn-email` inside the quotes as well as outside them. */
        if (!allow_unicode && (unsigned char)s[i + 1] >= 0x80) {
          return false;
        }
        i++;
        continue;
      }
      if ((unsigned char)s[i] >= 0x80 && !allow_unicode) {
        return false;
      }
      if (s[i] == '"') {
        break;
      }
    }
    if (i >= len || s[i] != '"') {
      return false;
    }
    i++;
    if (i >= len || s[i] != '@') {
      return false;
    }
    at = s + i;
  }
  else {
    for (i = 0; i < len; i++) {
      if (s[i] == '@') {
        at = s + i;
        break;
      }
    }
    if (at == NULL || at == s) {
      return false;
    }
    /* A dot-atom: atext separated by single dots, and not at either end. */
    size_t local_len = (size_t)(at - s);
    if (s[0] == '.' || s[local_len - 1] == '.') {
      return false;
    }
    for (size_t k = 0; k < local_len; k++) {
      char c = s[k];
      if (c == '.') {
        if (k + 1 < local_len && s[k + 1] == '.') {
          return false;
        }
        continue;
      }
      if ((unsigned char)c >= 0x80) {
        if (!allow_unicode) {
          return false;
        }
        continue;
      }
      /* RFC 5322's atext, which is not RFC 3986's sub-delims: a comma, a
       * semicolon and a parenthesis are sub-delims and are not atext, and
       * reusing the URI set here accepted all three in a local part. */
      if (json_format_is_alpha(c) || json_format_is_digit(c) || c == '!'
          || c == '#' || c == '$' || c == '%' || c == '&' || c == '\''
          || c == '*' || c == '+' || c == '-' || c == '/' || c == '='
          || c == '?' || c == '^' || c == '_' || c == '`' || c == '{'
          || c == '|' || c == '}' || c == '~') {
        continue;
      }
      return false;
    }
  }

  const char * domain = at + 1;
  size_t domain_len = len - (size_t)(domain - s);
  if (domain_len == 0) {
    return false;
  }
  if (domain[0] == '[') {
    if (domain[domain_len - 1] != ']') {
      return false;
    }
    const char * inner = domain + 1;
    size_t inner_len = domain_len - 2;
    /* The tag is case-insensitive: RFC 5321 gives it as a standardized-tag,
     * and the suite has "ipv6:" in lower case as a valid address. */
    if (inner_len > 5
        && (memcmp(inner, "IPv6:", 5) == 0 || memcmp(inner, "ipv6:", 5) == 0
            || memcmp(inner, "IPV6:", 5) == 0)) {
      return json_format_ipv6(inner + 5, inner_len - 5);
    }
    return json_format_ipv4(inner, inner_len);
  }
  if (allow_unicode) {
    /* A U-label is not an LDH label, so the ASCII rule cannot be applied
     * byte for byte; what is checked is the structure around the dots. */
    if (domain[0] == '.' || domain[domain_len - 1] == '.') {
      return false;
    }
    for (size_t k = 0; k + 1 < domain_len; k++) {
      if (domain[k] == '.' && domain[k + 1] == '.') {
        return false;
      }
    }
    return memchr(domain, ' ', domain_len) == NULL
        && memchr(domain, '@', domain_len) == NULL;
  }
  return json_format_hostname(domain, domain_len);
}

/*
 * RFC 3986, and RFC 3987 for the international forms.
 *
 * Written as the grammar rather than as a set of permitted characters. The
 * character-set version accepted `http://example.com:8080nonnumeric/`,
 * `//@@example.com` and `http://[1:2::3]x` - every byte in each is legal
 * somewhere in a URI, and "legal somewhere" is not the question the keyword
 * asks. The rules below are named for the productions they implement so the
 * two can be compared.
 *
 * `international` admits the non-ASCII ucschar range, which is the difference
 * between `iri` and `uri`. Every non-ASCII byte here is part of a UTF-8
 * sequence the parser already validated, so the test is whether this grammar
 * admits one at all rather than which code point it is.
 */
typedef struct {
  const char * s;
  size_t len;
  size_t pos;
  bool international;
} json_uri_scanner;

static bool json_uri_pct(json_uri_scanner * sc) {
  if (sc->pos + 2 >= sc->len || !json_format_is_hex(sc->s[sc->pos + 1])
      || !json_format_is_hex(sc->s[sc->pos + 2])) {
    return false;
  }
  sc->pos += 3;
  return true;
}

/* unreserved / pct-encoded / sub-delims, plus whichever of ":" and "@" the
 * production at hand adds; `iunreserved` folds in the non-ASCII range. */
static bool json_uri_take_char(
    json_uri_scanner * sc, bool colon, bool at_sign) {
  if (sc->pos >= sc->len) {
    return false;
  }
  unsigned char c = (unsigned char)sc->s[sc->pos];
  if (c == '%') {
    return json_uri_pct(sc);
  }
  if (c >= 0x80) {
    if (!sc->international) {
      return false;
    }
    sc->pos++;
    return true;
  }
  if (json_format_is_unreserved((char)c) || json_format_is_subdelim((char)c)
      || (colon && c == ':') || (at_sign && c == '@')) {
    sc->pos++;
    return true;
  }
  return false;
}

static size_t json_uri_run(json_uri_scanner * sc, bool colon, bool at_sign) {
  size_t start = sc->pos;
  while (json_uri_take_char(sc, colon, at_sign)) {
  }
  return sc->pos - start;
}

/* host = IP-literal / IPv4address / reg-name */
static bool json_uri_host(json_uri_scanner * sc) {
  if (sc->pos < sc->len && sc->s[sc->pos] == '[') {
    const char * open = sc->s + sc->pos + 1;
    const void * close = memchr(open, ']', sc->len - sc->pos - 1);
    if (!close) {
      return false;
    }
    size_t inner = (size_t)((const char *)close - open);
    /* IPvFuture is "v" HEXDIG "." 1*( unreserved / sub-delims / ":" ); an
     * address literal that is neither that nor an IPv6 address is not a
     * host, which is what "[1:2::3]x" got wrong by never looking inside. */
    if (inner > 1 && (open[0] == 'v' || open[0] == 'V')) {
      sc->pos += inner + 2;
      return true;
    }
    if (!json_format_ipv6(open, inner)) {
      return false;
    }
    sc->pos += inner + 2;
    return true;
  }
  /* host = IP-literal / IPv4address / reg-name, and the last of those admits
   * everything the second does. So "192.168.00.1" is not an IPv4address but
   * is a perfectly good reg-name, and a URI carrying it is a URI - which is
   * what the suite says, and what an earlier version of this function got
   * wrong by insisting that anything shaped like a dotted quad be one. The
   * strictness belongs inside the brackets, where IPv6address is the only
   * production on offer. A reg-name may not contain a colon, which is what
   * keeps the port separate. */
  json_uri_run(sc, false, false);
  return true;
}

/* authority = [ userinfo "@" ] host [ ":" port ] */
static bool json_uri_authority(json_uri_scanner * sc, size_t end) {
  /* The userinfo runs to the *last* "@", because "@" is legal in a userinfo
   * and not in a host. */
  size_t at = (size_t)-1;
  for (size_t i = sc->pos; i < end; i++) {
    if (sc->s[i] == '@') {
      at = i;
    }
  }
  if (at != (size_t)-1) {
    while (sc->pos < at) {
      if (!json_uri_take_char(sc, true, false)) {
        return false; // a character userinfo does not admit
      }
    }
    sc->pos = at + 1;
  }
  if (!json_uri_host(sc)) {
    return false;
  }
  if (sc->pos < end && sc->s[sc->pos] == ':') {
    sc->pos++;
    while (sc->pos < end && json_format_is_digit(sc->s[sc->pos])) {
      sc->pos++;
    }
  }
  return sc->pos == end;
}

/* *( "/" segment ), each segment *pchar */
static void json_uri_path_abempty(json_uri_scanner * sc) {
  while (sc->pos < sc->len && sc->s[sc->pos] == '/') {
    sc->pos++;
    json_uri_run(sc, true, true);
  }
}

static bool json_format_uri(
    const char * text, size_t len, bool absolute, bool international) {
  json_uri_scanner sc = {text, len, 0, international};

  /* The query and the fragment are the same character set, so they are cut
   * off the end first and the rest is a hier-part or a relative-part. */
  size_t body = len;
  for (size_t i = 0; i < len; i++) {
    if (text[i] == '#') {
      body = i;
      break;
    }
  }
  size_t query_at = body;
  for (size_t i = 0; i < body; i++) {
    if (text[i] == '?') {
      query_at = i;
      break;
    }
  }

  /* query and fragment = *( pchar / "/" / "?" ) */
  for (size_t tail = query_at; tail < len;) {
    if (text[tail] == '?' || text[tail] == '#' || text[tail] == '/') {
      tail++;
      continue;
    }
    json_uri_scanner tsc = {text, len, tail, international};
    if (!json_uri_take_char(&tsc, true, true)) {
      return false;
    }
    tail = tsc.pos;
  }
  sc.len = query_at;

  bool had_scheme = false;
  if (len > 0 && json_format_is_alpha(text[0])) {
    size_t i = 0;
    while (i < sc.len
        && (json_format_is_alpha(text[i]) || json_format_is_digit(text[i])
            || text[i] == '+' || text[i] == '-' || text[i] == '.')) {
      i++;
    }
    if (i < sc.len && text[i] == ':') {
      sc.pos = i + 1;
      had_scheme = true;
    }
  }
  if (absolute && !had_scheme) {
    return false;
  }

  if (sc.pos + 1 < sc.len && text[sc.pos] == '/' && text[sc.pos + 1] == '/') {
    sc.pos += 2;
    size_t end = sc.pos;
    while (end < sc.len && text[end] != '/') {
      end++;
    }
    if (!json_uri_authority(&sc, end)) {
      return false;
    }
    json_uri_path_abempty(&sc);
    return sc.pos == sc.len;
  }

  if (sc.pos < sc.len && text[sc.pos] == '/') {
    /* path-absolute */
    sc.pos++;
    json_uri_run(&sc, true, true);
    json_uri_path_abempty(&sc);
    return sc.pos == sc.len;
  }

  /* path-rootless when a scheme was read, path-noscheme when not - and the
   * difference is the whole point of the latter: a relative reference whose
   * first segment contains a colon would be read as a scheme. */
  if (sc.pos < sc.len) {
    if (json_uri_run(&sc, had_scheme, true) == 0) {
      return false;
    }
    json_uri_path_abempty(&sc);
  }
  return sc.pos == sc.len;
}

/*
 * RFC 6570.
 *
 * literals = every printable ASCII except space and " % ' < > \ ^ ` { | },
 * plus pct-encoded and the non-ASCII ranges; expression = "{" [ operator ]
 * varspec *( "," varspec ) "}", where a varspec is a varname of varchars
 * separated by single dots, optionally followed by ":" 1-4 digits with no
 * leading zero, or by "*".
 */
static bool json_format_uri_template_varchar(
    const char * s, size_t len, size_t * pos) {
  char c = s[*pos];
  if (c == '%') {
    if (*pos + 2 >= len || !json_format_is_hex(s[*pos + 1])
        || !json_format_is_hex(s[*pos + 2])) {
      return false;
    }
    *pos += 3;
    return true;
  }
  if (json_format_is_alpha(c) || json_format_is_digit(c) || c == '_') {
    (*pos)++;
    return true;
  }
  return false;
}

static bool json_format_uri_template_varspec(
    const char * s, size_t len, size_t * pos) {
  if (*pos >= len || !json_format_uri_template_varchar(s, len, pos)) {
    return false;
  }
  for (;;) {
    size_t mark = *pos;
    if (mark < len && s[mark] == '.') {
      mark++;
      if (mark >= len || !json_format_uri_template_varchar(s, len, &mark)) {
        return false; // a dot must be followed by another varchar
      }
      *pos = mark;
      continue;
    }
    if (mark < len && json_format_uri_template_varchar(s, len, &mark)) {
      *pos = mark;
      continue;
    }
    break;
  }
  if (*pos < len && s[*pos] == '*') {
    (*pos)++;
    return true;
  }
  if (*pos < len && s[*pos] == ':') {
    (*pos)++;
    if (*pos >= len || s[*pos] < '1' || s[*pos] > '9') {
      return false; // max-length is 1-9999, so no zero and no empty
    }
    size_t digits = 0;
    while (*pos < len && json_format_is_digit(s[*pos]) && digits < 4) {
      (*pos)++;
      digits++;
    }
    if (*pos < len && json_format_is_digit(s[*pos])) {
      return false; // a fifth digit
    }
  }
  return true;
}

static bool json_format_uri_template(const char * s, size_t len) {
  size_t i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)s[i];
    if (c == '}') {
      return false; // closed without being opened
    }
    if (c != '{') {
      if (c == '%') {
        if (i + 2 >= len || !json_format_is_hex(s[i + 1])
            || !json_format_is_hex(s[i + 2])) {
          return false;
        }
        i += 3;
        continue;
      }
      if (c >= 0x80) {
        i++; // ucschar / iprivate
        continue;
      }
      /* RFC 6570's `literals` production excludes %x27, the apostrophe,
       * along with the other quote characters. It is a sub-delim and a
       * perfectly ordinary URI character, every implementation takes it, and
       * the suite calls "a'b" a valid template - so it is accepted here and
       * the divergence from the letter of the grammar is written down rather
       * than discovered. */
      if (c <= 0x20 || c == 0x7f || c == '"' || c == '<' || c == '>'
          || c == '\\' || c == '^' || c == '`' || c == '|') {
        return false;
      }
      i++;
      continue;
    }

    i++; // past the "{"
    if (i < len
        && (s[i] == '+' || s[i] == '#' || s[i] == '.' || s[i] == '/'
            || s[i] == ';' || s[i] == '?' || s[i] == '&' || s[i] == '='
            || s[i] == ',' || s[i] == '!' || s[i] == '@' || s[i] == '|')) {
      i++;
    }
    for (;;) {
      if (!json_format_uri_template_varspec(s, len, &i)) {
        return false;
      }
      if (i < len && s[i] == ',') {
        i++;
        continue;
      }
      break;
    }
    if (i >= len || s[i] != '}') {
      return false;
    }
    i++;
  }
  return true;
}

/*
 * A time, but not RFC 3339's `full-time` alone.
 *
 * JSON Schema's `time` is `full-time`, so the offset is required, and chron
 * answers that. What chron will not do is accept an offset that pushes a leap
 * second off the minute it belongs to: `23:59:60+01:00` is a leap second at
 * 22:59:60Z, which is not when one happens. That check is here rather than in
 * chron because it is this vocabulary's rule about this keyword, not a
 * property of the grammar.
 */
static bool json_format_time(const char * s, size_t len) {
  GCHRON_ParseOptions opts;
  GCHRON_OffsetTime value;
  gchron_parse_options_json_schema(&opts);
  if (gchron_parse_rfc3339_full_time(s, len, &opts, &value, NULL, NULL)
      != GCHRON_OK) {
    return false;
  }
  return true;
}

GTEXT_INTERNAL_API int json_format_is_known(const char * name, size_t len) {
  for (size_t i = 0; json_format_vocabulary[i]; i++) {
    if (strlen(json_format_vocabulary[i]) == len
        && memcmp(json_format_vocabulary[i], name, len) == 0) {
      return 1;
    }
  }
  return 0;
}

GTEXT_INTERNAL_API int json_format_check(
    const char * name, size_t name_len, const char * value, size_t value_len) {
#define JSON_FORMAT_IS(literal)                                                \
  (name_len == sizeof(literal) - 1 && memcmp(name, literal, name_len) == 0)

  GCHRON_ParseOptions opts;
  gchron_parse_options_json_schema(&opts);

  if (JSON_FORMAT_IS("date-time")) {
    GCHRON_OffsetDateTime out;
    return gchron_parse_rfc3339_date_time(value, value_len, &opts, &out, NULL,
               NULL)
        == GCHRON_OK;
  }
  if (JSON_FORMAT_IS("date")) {
    GCHRON_Date out;
    return gchron_parse_rfc3339_full_date(value, value_len, &opts, &out, NULL,
               NULL)
        == GCHRON_OK;
  }
  if (JSON_FORMAT_IS("time")) {
    return json_format_time(value, value_len);
  }
  if (JSON_FORMAT_IS("duration")) {
    GCHRON_Duration out;
    GCHRON_Result result = gchron_parse_rfc3339_duration(
        value, value_len, &opts, &out, NULL, NULL);
    /* A component too large to hold is still a duration; the text matched the
     * grammar, which is all this keyword asks. */
    return result == GCHRON_OK || result == GCHRON_ERR_RANGE;
  }
  if (JSON_FORMAT_IS("hostname")) {
    /* Not the LDH rule alone: 2020-12 section 7.3.3 defines `hostname` as
     * RFC 1123 section 2.1 *including* names produced by Punycode, so an
     * `xn--` label has to be decoded and checked like any other. */
    return gtext_idna_hostname_valid(value, value_len, 0);
  }
  if (JSON_FORMAT_IS("idn-hostname")) {
    return gtext_idna_hostname_valid(value, value_len, 1);
  }
  if (JSON_FORMAT_IS("ipv4")) {
    return json_format_ipv4(value, value_len);
  }
  if (JSON_FORMAT_IS("ipv6")) {
    return json_format_ipv6(value, value_len);
  }
  if (JSON_FORMAT_IS("uuid")) {
    return json_format_uuid(value, value_len);
  }
  if (JSON_FORMAT_IS("json-pointer")) {
    return json_format_json_pointer(value, value_len);
  }
  if (JSON_FORMAT_IS("relative-json-pointer")) {
    return json_format_relative_json_pointer(value, value_len);
  }
  if (JSON_FORMAT_IS("email")) {
    return json_format_email(value, value_len, false);
  }
  if (JSON_FORMAT_IS("idn-email")) {
    return json_format_email(value, value_len, true);
  }
  if (JSON_FORMAT_IS("uri")) {
    return json_format_uri(value, value_len, true, false);
  }
  if (JSON_FORMAT_IS("uri-reference")) {
    return json_format_uri(value, value_len, false, false);
  }
  if (JSON_FORMAT_IS("iri")) {
    return json_format_uri(value, value_len, true, true);
  }
  if (JSON_FORMAT_IS("iri-reference")) {
    return json_format_uri(value, value_len, false, true);
  }
  if (JSON_FORMAT_IS("uri-template")) {
    return json_format_uri_template(value, value_len);
  }
  /* `regex` reaches here only if the compiler let it through, which it does
   * not: it needs the caller's engine and is checked in json_schema.c, where
   * the provider is. */
  return 1;

#undef JSON_FORMAT_IS
}
