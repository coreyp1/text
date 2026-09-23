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
 * Guessing a dialect from a sample - the equivalent of Python's `csv.Sniffer`.
 *
 * **It guesses by parsing, not by counting characters.** For each candidate
 * delimiter and quote character it parses the sample with that dialect and asks
 * how regular the result is; the candidate whose parse gives the most rows of
 * equal width wins. Counting occurrences is the obvious approach and it is
 * wrong on the inputs that matter:
 *
 *   name,"Smith; John",42     a `;` inside a quoted field outnumbers nothing,
 *                             but a frequency count sees one of each
 *   a;b;c,d                   `,` occurs once and `;` twice, and a count has to
 *                             invent a tie-break; a parse says `;` gives three
 *                             fields of a consistent width and `,` gives two
 *                             ragged ones
 *
 * A parse also gets quoting for free, because it is the same parser that will
 * read the document afterwards. A sniffer that disagrees with its own parser is
 * worse than none.
 *
 * **It refuses rather than guesses when the sample cannot decide.** A sample
 * with one field per row under every candidate names no delimiter; a sample
 * where two candidates score identically names no single one. Both are
 * GTEXT_CSV_E_INVALID with a message saying which, because a caller who gets a
 * confident wrong answer has no way to tell, and the cost of that is a whole
 * file read into the wrong shape.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <ghoti.io/text/csv/csv_core.h>
#include <ghoti.io/text/csv/csv_table.h>
#include <ghoti.io/text/macros.h>

#include "csv_internal.h"

/* The delimiters worth trying, in the order a tie would prefer them. Comma
   first because it is the format's name; the rest are what spreadsheets and
   database exports actually emit. A caller whose delimiter is not here can
   still set one by hand - sniffing is a convenience, not the only route. */
static const char csv_sniff_delimiters[] = {',', ';', '\t', '|', ':'};

/* Both quote characters in the wild. Tried in this order so that a sample with
   no quotes at all reports the RFC 4180 one. */
static const char csv_sniff_quotes[] = {'"', '\''};

typedef struct {
  size_t rows;         ///< Complete records the parse produced
  size_t modal_width;  ///< The most common field count
  size_t modal_rows;   ///< How many rows had it
  bool parsed;         ///< Whether the parse succeeded at all
} csv_sniff_score;

/*
 * Score one candidate dialect.
 *
 * The last record of a sample is dropped when the sample does not end at a
 * record boundary: a truncated final row is narrower than the rest through no
 * fault of the delimiter, and letting it count makes every candidate look
 * ragged. This is the difference between sniffing a whole file and sniffing the
 * first few kilobytes of one, and the second is the case that matters.
 */
static csv_sniff_score csv_sniff_try(const char * data, size_t len,
    char delimiter, char quote, bool ends_at_boundary,
    bool accept_lf, bool accept_crlf, bool accept_cr) {
  csv_sniff_score score = {0, 0, 0, false};

  GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
  opts.dialect.delimiter = delimiter;
  opts.dialect.quote = quote;
  /* The newline style the sample actually uses, decided before any scoring
     parse and passed in here. Scoring with the default instead made a
     CR-delimited sample unscoreable: with accept_cr off a lone CR is not a
     record separator but is still refused inside an unquoted field, so every
     candidate failed to parse and the sniffer reported that nothing split the
     sample. The newline style is knowable by looking, so there is no reason for
     the delimiter search to be guessing at it too. */
  opts.dialect.accept_lf = accept_lf;
  opts.dialect.accept_crlf = accept_crlf;
  opts.dialect.accept_cr = accept_cr;
  // A sample is untrusted text of unknown dialect, so the scoring parse must
  // not refuse it for reasons that are not about the delimiter. Quotes that a
  // stricter reader would reject say nothing about which delimiter is right.
  opts.dialect.allow_unquoted_quotes = true;
  opts.validate_utf8 = false;
  opts.enable_context_snippet = false;

  GTEXT_CSV_Table * table = gtext_csv_parse_table(data, len, &opts, NULL);
  if (!table) {
    return score;
  }
  score.parsed = true;

  size_t rows = gtext_csv_row_count(table);
  if (rows > 1 && !ends_at_boundary) {
    rows--; // drop the truncated tail
  }
  score.rows = rows;

  /* The modal width, counted without allocating: for each row, count how many
     rows share its width. O(n^2) in the number of rows, which is fine for a
     sample - and the alternative is a histogram allocation in a function whose
     whole job is to be cheap enough to call before parsing. */
  for (size_t i = 0; i < rows; i++) {
    size_t width = gtext_csv_col_count(table, i);
    size_t same = 0;
    for (size_t j = 0; j < rows; j++) {
      if (gtext_csv_col_count(table, j) == width) {
        same++;
      }
    }
    if (same > score.modal_rows
        || (same == score.modal_rows && width > score.modal_width)) {
      score.modal_rows = same;
      score.modal_width = width;
    }
  }

  gtext_csv_free_table(table);
  return score;
}

/* Is `a` a better answer than `b`?
 *
 * Regularity first - the fraction of rows at the modal width - because that is
 * what "this is the delimiter" means. Width second, and only as a tie-break:
 * without it, a delimiter that appears nowhere scores a perfect 1.0 with one
 * field per row, which is how a naive scorer concludes that every document is
 * single-column. Width alone would be worse still, preferring whichever
 * character happens to be most frequent inside the text. */
static bool csv_sniff_better(
    const csv_sniff_score * a, const csv_sniff_score * b) {
  if (!a->parsed || a->modal_width < 2) {
    return false;
  }
  if (!b->parsed || b->modal_width < 2) {
    return true;
  }
  // Compare modal_rows/rows without floating point.
  size_t lhs = a->modal_rows * b->rows;
  size_t rhs = b->modal_rows * a->rows;
  if (lhs != rhs) {
    return lhs > rhs;
  }
  return a->modal_width > b->modal_width;
}

static bool csv_sniff_equal(
    const csv_sniff_score * a, const csv_sniff_score * b) {
  if (!a->parsed || !b->parsed) {
    return false;
  }
  return a->modal_rows * b->rows == b->modal_rows * a->rows
      && a->modal_width == b->modal_width;
}

GTEXT_API GTEXT_CSV_Status gtext_csv_sniff(const void * data, size_t len,
    GTEXT_CSV_Dialect * out, GTEXT_CSV_Error * err) {
  if (!data || !out) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID, "data and out must not be NULL");
    return GTEXT_CSV_E_INVALID;
  }
  if (len == 0) {
    CSV_SET_ERROR(
        err, GTEXT_CSV_E_INVALID, "an empty sample names no dialect");
    return GTEXT_CSV_E_INVALID;
  }

  const char * bytes = (const char *)data;

  /* Newline style is decided by looking, not by scoring: it is unambiguous
     wherever it appears, unlike the delimiter. A CR followed by an LF is a
     CRLF; a CR that is not is a lone CR, which is the old Mac convention and
     off by default because accepting it changes what a CR inside a field
     means. */
  bool saw_crlf = false;
  bool saw_lf = false;
  bool saw_cr = false;
  for (size_t i = 0; i < len; i++) {
    if (bytes[i] == '\r') {
      if (i + 1 < len && bytes[i + 1] == '\n') {
        saw_crlf = true;
        i++;
      }
      else {
        saw_cr = true;
      }
    }
    else if (bytes[i] == '\n') {
      saw_lf = true;
    }
  }

  // Does the sample stop on a record boundary? If not, its last record is
  // truncated and must not be scored.
  bool ends_at_boundary =
      (bytes[len - 1] == '\n' || bytes[len - 1] == '\r');

  // What the dialect will say, and what the scoring parses are told.
  bool accept_lf = saw_lf || !(saw_crlf || saw_cr);
  bool accept_crlf = saw_crlf || !(saw_lf || saw_cr);
  bool accept_cr = saw_cr;

  csv_sniff_score best = {0, 0, 0, false};
  char best_delim = 0;
  char best_quote = csv_sniff_quotes[0];
  bool ambiguous = false;

  for (size_t q = 0; q < sizeof(csv_sniff_quotes); q++) {
    for (size_t d = 0; d < sizeof(csv_sniff_delimiters); d++) {
      csv_sniff_score score = csv_sniff_try(bytes, len,
          csv_sniff_delimiters[d], csv_sniff_quotes[q], ends_at_boundary,
          accept_lf, accept_crlf, accept_cr);
      if (csv_sniff_better(&score, &best)) {
        best = score;
        best_delim = csv_sniff_delimiters[d];
        best_quote = csv_sniff_quotes[q];
        ambiguous = false;
      }
      else if (best_delim && csv_sniff_delimiters[d] != best_delim
          && csv_sniff_equal(&score, &best) && score.modal_width >= 2) {
        // Two different delimiters explain the sample equally well. A different
        // *quote* character scoring the same is not ambiguity - a sample with
        // no quotes in it scores identically under both, and the first one
        // named wins by the order of the table.
        ambiguous = true;
      }
    }
  }

  if (!best_delim) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID,
        "no candidate delimiter splits the sample into more than one field");
    return GTEXT_CSV_E_INVALID;
  }
  if (ambiguous) {
    CSV_SET_ERROR(err, GTEXT_CSV_E_INVALID,
        "the sample is explained equally well by more than one delimiter");
    return GTEXT_CSV_E_INVALID;
  }

  *out = gtext_csv_dialect_default();
  out->delimiter = best_delim;
  out->quote = best_quote;
  out->accept_lf = accept_lf;
  out->accept_crlf = accept_crlf;
  out->accept_cr = accept_cr;
  return GTEXT_CSV_OK;
}
