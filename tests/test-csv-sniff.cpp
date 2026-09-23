/**
 * @file
 *
 * Guessing a CSV dialect from a sample.
 *
 * The cases that matter are the ones a character-frequency sniffer gets wrong,
 * because that is the design decision this implementation makes: it parses with
 * each candidate rather than counting occurrences.  A delimiter inside a quoted
 * field, and a sample where the wrong delimiter is the more frequent character,
 * are the two shapes that separate the approaches.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <gtest/gtest.h>

#include <ghoti.io/text/csv.h>

namespace {

// Sniff, requiring success, and return the delimiter.
char sniffed_delimiter(const std::string & sample) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_CSV_Status st =
	    gtext_csv_sniff(sample.data(), sample.size(), &d, &err);
	EXPECT_EQ(st, GTEXT_CSV_OK)
	    << "sample [" << sample << "]: "
	    << (err.message ? err.message : "no message");
	gtext_csv_error_free(&err);
	return st == GTEXT_CSV_OK ? d.delimiter : '\0';
}

} // namespace

TEST(CsvSniff, FindsTheOrdinaryDelimiters) {
	EXPECT_EQ(sniffed_delimiter("a,b,c\n1,2,3\n4,5,6\n"), ',');
	EXPECT_EQ(sniffed_delimiter("a;b;c\n1;2;3\n4;5;6\n"), ';');
	EXPECT_EQ(sniffed_delimiter("a\tb\tc\n1\t2\t3\n4\t5\t6\n"), '\t');
	EXPECT_EQ(sniffed_delimiter("a|b|c\n1|2|3\n4|5|6\n"), '|');
	EXPECT_EQ(sniffed_delimiter("a:b:c\n1:2:3\n4:5:6\n"), ':');
}

/*
 * The case a frequency count cannot get right. The `;` characters are inside
 * quoted fields, so they outnumber the real delimiter two to one, and only a
 * parse can see that they are not structural.
 */
TEST(CsvSniff, IsNotFooledByADelimiterInsideQuotes) {
	EXPECT_EQ(sniffed_delimiter(
	              "name,note\n\"Smith; John\",\"a; b; c\"\n\"Doe; Jane\",\"x; y\"\n"),
	    ',')
	    << "a semicolon inside a quoted field is not a delimiter";

	// And the other way round: commas inside quotes, semicolon delimiting.
	EXPECT_EQ(sniffed_delimiter(
	              "name;note\n\"Smith, John\";\"a, b, c\"\n\"Doe, Jane\";\"x, y\"\n"),
	    ';');
}

/*
 * The other shape a count gets wrong: the wrong candidate is more frequent, but
 * only the right one produces rows of a consistent width.
 */
TEST(CsvSniff, PrefersRegularityOverFrequency) {
	// `,` appears six times and `;` three, but splitting on `,` gives widths
	// 2,3,2 while splitting on `;` gives 2,2,2.
	const std::string sample = "a,b;c\nd,e,f;g\nh,i;j\n";
	EXPECT_EQ(sniffed_delimiter(sample), ';')
	    << "the more frequent character is not the delimiter here";
}

TEST(CsvSniff, ReadsTheNewlineStyleFromTheSample) {
	GTEXT_CSV_Dialect d;
	GTEXT_CSV_Error err;

	std::memset(&d, 0, sizeof(d));
	std::memset(&err, 0, sizeof(err));
	const std::string crlf = "a,b\r\n1,2\r\n";
	ASSERT_EQ(gtext_csv_sniff(crlf.data(), crlf.size(), &d, &err),
	    GTEXT_CSV_OK);
	EXPECT_TRUE(d.accept_crlf);
	EXPECT_FALSE(d.accept_lf) << "no bare LF in the sample";
	EXPECT_FALSE(d.accept_cr);
	gtext_csv_error_free(&err);

	std::memset(&d, 0, sizeof(d));
	std::memset(&err, 0, sizeof(err));
	const std::string lf = "a,b\n1,2\n";
	ASSERT_EQ(gtext_csv_sniff(lf.data(), lf.size(), &d, &err), GTEXT_CSV_OK);
	EXPECT_TRUE(d.accept_lf);
	EXPECT_FALSE(d.accept_crlf);
	EXPECT_FALSE(d.accept_cr);
	gtext_csv_error_free(&err);

	// A lone CR is only accepted when one is actually present, because
	// accepting it changes what a CR inside a field means.
	std::memset(&d, 0, sizeof(d));
	std::memset(&err, 0, sizeof(err));
	const std::string cr = "a,b\r1,2\r";
	ASSERT_EQ(gtext_csv_sniff(cr.data(), cr.size(), &d, &err), GTEXT_CSV_OK);
	EXPECT_TRUE(d.accept_cr);
	gtext_csv_error_free(&err);
}

TEST(CsvSniff, FindsASingleQuoteDialect) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	const std::string sample =
	    "name,note\n'Smith, John','a, b'\n'Doe, Jane','x, y'\n";
	ASSERT_EQ(gtext_csv_sniff(sample.data(), sample.size(), &d, &err),
	    GTEXT_CSV_OK)
	    << (err.message ? err.message : "");
	EXPECT_EQ(d.delimiter, ',');
	EXPECT_EQ(d.quote, '\'');
	gtext_csv_error_free(&err);
}

/* A sample with no quotes scores the same under both quote characters, and that
   is not ambiguity - the RFC 4180 one is reported, by the order of the table.
   Without this, every unquoted document would be refused. */
TEST(CsvSniff, NoQuotesMeansTheDefaultQuoteNotAnAmbiguity) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	const std::string sample = "a,b,c\n1,2,3\n";
	ASSERT_EQ(gtext_csv_sniff(sample.data(), sample.size(), &d, &err),
	    GTEXT_CSV_OK)
	    << (err.message ? err.message : "");
	EXPECT_EQ(d.quote, '"');
	gtext_csv_error_free(&err);
}

/*
 * The refusals. A sniffer that always answers is worse than one that says it
 * cannot tell, because a confident wrong answer reads a whole file into the
 * wrong shape and the caller has nothing to check.
 */
TEST(CsvSniff, RefusesWhenNothingSplitsTheSample) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	const std::string sample = "one\ntwo\nthree\n";
	EXPECT_EQ(gtext_csv_sniff(sample.data(), sample.size(), &d, &err),
	    GTEXT_CSV_E_INVALID);
	ASSERT_NE(err.message, nullptr);
	EXPECT_NE(std::string(err.message).find("more than one field"),
	    std::string::npos)
	    << err.message;
	gtext_csv_error_free(&err);
}

TEST(CsvSniff, RefusesWhenTwoDelimitersExplainItEquallyWell) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	// One row split by `,` and one by `;`: each candidate explains exactly one
	// of the two rows, at the same width. Nothing in the bytes prefers either,
	// and a sniffer that picked one would be inventing a reason.
	const std::string sample = "a,b\nc;d\n";
	GTEXT_CSV_Status st =
	    gtext_csv_sniff(sample.data(), sample.size(), &d, &err);
	if (st == GTEXT_CSV_OK) {
		// If the scorer ever does separate them, say so loudly rather than
		// letting the test quietly stop testing anything.
		FAIL() << "expected an ambiguity; got delimiter '" << d.delimiter
		       << "'. If the scoring changed deliberately, this case needs "
		          "replacing with one that is still ambiguous.";
	}
	EXPECT_EQ(st, GTEXT_CSV_E_INVALID);
	ASSERT_NE(err.message, nullptr);
	EXPECT_NE(std::string(err.message).find("more than one delimiter"),
	    std::string::npos)
	    << err.message;
	gtext_csv_error_free(&err);
}

TEST(CsvSniff, RefusesAnEmptyOrNullSample) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	EXPECT_EQ(gtext_csv_sniff("", 0, &d, nullptr), GTEXT_CSV_E_INVALID);
	EXPECT_EQ(gtext_csv_sniff(nullptr, 5, &d, nullptr), GTEXT_CSV_E_INVALID);
	EXPECT_EQ(gtext_csv_sniff("a,b\n", 4, nullptr, nullptr),
	    GTEXT_CSV_E_INVALID);
}

/*
 * A truncated tail must not count against the delimiter, because sniffing the
 * first few kilobytes of a large file is the case this exists for. The sample
 * below stops in the middle of its last row.
 */
TEST(CsvSniff, IgnoresATruncatedFinalRecord) {
	EXPECT_EQ(sniffed_delimiter("a,b,c\n1,2,3\n4,5,6\n7,8"), ',')
	    << "the partial last row should not make the sample look ragged";

	// And with a delimiter that only appears in complete rows.
	EXPECT_EQ(sniffed_delimiter("a;b;c\n1;2;3\n4;5;6\n7;8"), ';');
}

/*
 * The whole point: what the sniffer returns must actually parse the document.
 * A dialect that scores well and then fails to read the file would be worse
 * than useless, so the round trip is asserted rather than assumed.
 */
TEST(CsvSniff, WhatItReturnsParsesTheDocument) {
	struct Case {
		const char * label;
		std::string doc;
		size_t rows;
		size_t cols;
	} cases[] = {
	    {"comma", "a,b,c\n1,2,3\n", 2, 3},
	    {"semicolon", "a;b;c\n1;2;3\n", 2, 3},
	    {"tab", "a\tb\n1\t2\n", 2, 2},
	    {"pipe with quotes", "a|b\n\"x|y\"|z\n", 2, 2},
	    {"crlf", "a,b\r\n1,2\r\n", 2, 2},
	    {"quoted newline", "a,b\n\"multi\nline\",2\n", 2, 2},
	};

	for (const Case & c : cases) {
		GTEXT_CSV_Dialect d;
		std::memset(&d, 0, sizeof(d));
		GTEXT_CSV_Error err;
		std::memset(&err, 0, sizeof(err));
		ASSERT_EQ(gtext_csv_sniff(c.doc.data(), c.doc.size(), &d, &err),
		    GTEXT_CSV_OK)
		    << c.label << ": " << (err.message ? err.message : "");
		gtext_csv_error_free(&err);

		GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
		opts.dialect = d;
		std::memset(&err, 0, sizeof(err));
		GTEXT_CSV_Table * t =
		    gtext_csv_parse_table(c.doc.data(), c.doc.size(), &opts, &err);
		ASSERT_NE(t, nullptr)
		    << c.label << ": the sniffed dialect failed to parse: "
		    << (err.message ? err.message : "");
		EXPECT_EQ(gtext_csv_row_count(t), c.rows) << c.label;
		EXPECT_EQ(gtext_csv_col_count(t, 0), c.cols) << c.label;
		gtext_csv_free_table(t);
		gtext_csv_error_free(&err);
	}
}

/* Header detection is deliberately absent, and the dialect field it would set
   must stay off - a caller who asked for a guess and silently got a header row
   removed would lose a row of data. */
TEST(CsvSniff, DoesNotGuessWhetherThereIsAHeader) {
	GTEXT_CSV_Dialect d;
	std::memset(&d, 0, sizeof(d));
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	// As header-like as a sample gets: words above numbers.
	const std::string sample = "name,age,city\nAda,36,London\nGrace,45,NYC\n";
	ASSERT_EQ(gtext_csv_sniff(sample.data(), sample.size(), &d, &err),
	    GTEXT_CSV_OK);
	EXPECT_FALSE(d.treat_first_row_as_header)
	    << "whether the first row names the columns is not a grammar question";
	gtext_csv_error_free(&err);
}
