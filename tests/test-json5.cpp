/**
 * @file
 *
 * The JSON5 extensions, one option at a time.
 *
 * JSON5 is a dialect rather than a flag, so each of its differences from JSON
 * is a separate option here and each is tested in both directions: the form is
 * accepted when its option is on, and refused when it is off. A test that only
 * checked acceptance would pass just as well against a parser that accepted
 * the form unconditionally, which is the mistake that matters - strict JSON is
 * the default and has to stay refusing.
 *
 * Every case that can go through the streaming parser does. The two parsers
 * share the lexer but not the grammar, and the lexer's number scan carries
 * state across a chunk boundary, so a hex literal or a signed Infinity split
 * between feeds is the case most likely to be wrong.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <gtest/gtest.h>

#include <ghoti.io/text/json.h>

namespace {

GTEXT_JSON_Parse_Options number_opts(void) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.parse_int64 = true;
	opts.parse_uint64 = true;
	opts.parse_double = true;
	opts.preserve_number_lexeme = true;
	return opts;
}

// Parse and return the status, freeing whatever came back.
GTEXT_JSON_Status dom_status(
    const std::string & src, const GTEXT_JSON_Parse_Options * opts) {
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * v = gtext_json_parse(src.data(), src.size(), opts, &err);
	GTEXT_JSON_Status status = v ? GTEXT_JSON_OK : err.code;
	if (v) {
		gtext_json_free(v);
	}
	gtext_json_error_free(&err);
	return status;
}

// Feed the streaming parser in chunks of `chunk` bytes and report whether the
// whole document was accepted.
bool stream_accepts(const std::string & src,
    const GTEXT_JSON_Parse_Options * opts, size_t chunk) {
	GTEXT_JSON_Event_cb cb = [](void *, const GTEXT_JSON_Event *,
	                             GTEXT_JSON_Error *) { return GTEXT_JSON_OK; };
	GTEXT_JSON_Stream * st = gtext_json_stream_new(opts, cb, nullptr);
	if (!st) {
		return false;
	}
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Status status = GTEXT_JSON_OK;
	for (size_t i = 0; i < src.size(); i += chunk) {
		size_t n = std::min(chunk, src.size() - i);
		status = gtext_json_stream_feed(st, src.data() + i, n, &err);
		if (status != GTEXT_JSON_OK) {
			break;
		}
	}
	if (status == GTEXT_JSON_OK) {
		status = gtext_json_stream_finish(st, &err);
	}
	gtext_json_stream_free(st);
	gtext_json_error_free(&err);
	return status == GTEXT_JSON_OK;
}

// Both parsers, and the streaming one byte at a time as well as whole.
void expect_both(const std::string & src,
    const GTEXT_JSON_Parse_Options * opts, bool valid) {
	EXPECT_EQ(dom_status(src, opts) == GTEXT_JSON_OK, valid)
	    << "DOM parser, input [" << src << "]";
	for (size_t chunk : {src.size() ? src.size() : size_t(1), size_t(1)}) {
		EXPECT_EQ(stream_accepts(src, opts, chunk), valid)
		    << "streaming parser, chunk " << chunk << ", input [" << src << "]";
	}
}

// The one number in a single-element array.
const GTEXT_JSON_Value * only_number(const GTEXT_JSON_Value * doc) {
	if (!doc || gtext_json_typeof(doc) != GTEXT_JSON_ARRAY) {
		return nullptr;
	}
	return gtext_json_array_get(doc, 0);
}

} // namespace

// ===========================================================================
// Hexadecimal integers
// ===========================================================================

TEST(Json5Hex, ValuesAndSpellings) {
	struct Case {
		const char * src;
		uint64_t u64;
		int64_t i64;
		double dbl;
		bool has_u64;
	};
	const Case cases[] = {
		{"[0x0]", 0, 0, 0.0, true},
		{"[0x1F]", 31, 31, 31.0, true},
		{"[0X1f]", 31, 31, 31.0, true},
		{"[0xdeadBEEF]", 0xdeadBEEFull, 0xdeadBEEF, 3735928559.0, true},
		{"[0xff]", 255, 255, 255.0, true},
		// The sign belongs to the number, not to the digits.
		{"[-0x10]", 0, -16, -16.0, false},
	};

	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_hex_numbers = true;

	for (const Case & c : cases) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * doc =
		    gtext_json_parse(c.src, std::strlen(c.src), &opts, &err);
		ASSERT_NE(doc, nullptr)
		    << c.src << ": " << (err.message ? err.message : "no message");
		const GTEXT_JSON_Value * n = only_number(doc);
		ASSERT_NE(n, nullptr) << c.src;
		EXPECT_EQ(gtext_json_typeof(n), GTEXT_JSON_NUMBER) << c.src;

		int64_t i64 = 0;
		EXPECT_EQ(gtext_json_get_i64(n, &i64), GTEXT_JSON_OK) << c.src;
		EXPECT_EQ(i64, c.i64) << c.src;

		uint64_t u64 = 0;
		if (c.has_u64) {
			EXPECT_EQ(gtext_json_get_u64(n, &u64), GTEXT_JSON_OK) << c.src;
			EXPECT_EQ(u64, c.u64) << c.src;
		}

		double d = 0.0;
		EXPECT_EQ(gtext_json_get_double(n, &d), GTEXT_JSON_OK) << c.src;
		EXPECT_EQ(d, c.dbl) << c.src;

		gtext_json_free(doc);
		gtext_json_error_free(&err);
	}
}

/* The lexeme is what was written, not what it means: a writer asked to
   round-trip the document has to be able to reproduce the source spelling. */
TEST(Json5Hex, TheLexemeKeepsTheSpelling) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_hex_numbers = true;

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	const char * src = "[0XdeadBEEF]";
	GTEXT_JSON_Value * doc =
	    gtext_json_parse(src, std::strlen(src), &opts, &err);
	ASSERT_NE(doc, nullptr);
	const GTEXT_JSON_Value * n = only_number(doc);
	ASSERT_NE(n, nullptr);
	const char * lexeme = nullptr;
	size_t lexeme_len = 0;
	ASSERT_EQ(gtext_json_get_number_lexeme(n, &lexeme, &lexeme_len),
	    GTEXT_JSON_OK);
	EXPECT_EQ(std::string(lexeme, lexeme_len), "0XdeadBEEF");
	gtext_json_free(doc);
	gtext_json_error_free(&err);
}

/* 'e' is a hex digit, not an exponent marker, and '.' cannot open a fraction
   in a hex literal. Both are cases where reusing the decimal scan would give a
   wrong answer rather than an error. */
TEST(Json5Hex, ELooksLikeAnExponentAndIsADigit) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_hex_numbers = true;

	/* 0x1e2 is the case that separates the two readings: 482 if 'e' is a
	   digit, 100 if it is an exponent marker. */
	struct Case {
		const char * src;
		int64_t value;
	};
	const Case cases[] = {
	    {"[0xe]", 14},
	    {"[0x1e2]", 482},
	    {"[0x1ee]", 494},
	    {"[0xEEE]", 3822},
	    {"[0x1ee5]", 7909},
	};
	for (const Case & c : cases) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * doc =
		    gtext_json_parse(c.src, std::strlen(c.src), &opts, &err);
		ASSERT_NE(doc, nullptr)
		    << c.src << ": " << (err.message ? err.message : "no message");
		int64_t i64 = 0;
		ASSERT_EQ(gtext_json_get_i64(only_number(doc), &i64), GTEXT_JSON_OK)
		    << c.src;
		EXPECT_EQ(i64, c.value) << c.src;
		gtext_json_free(doc);
		gtext_json_error_free(&err);
	}

	// A fraction is not part of the hex grammar, so this is not a number.
	EXPECT_NE(dom_status("[0x1.8]", &opts), GTEXT_JSON_OK);
	// Neither is an exponent written the decimal way.
	EXPECT_NE(dom_status("[0x1p4]", &opts), GTEXT_JSON_OK);
}

TEST(Json5Hex, RefusedSpellings) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_hex_numbers = true;
	const char * refused[] = {
	    "[0x]",     // no digits
	    "[0xg]",    // not a hex digit
	    "[x10]",    // no leading zero
	    "[00x10]",  // two leading zeros
	    "[0x10x]",  // a second x
	    "[1x10]",   // the x is not after a zero
	};
	for (const char * src : refused) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5Hex, RefusedWhenTheOptionIsOff) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	EXPECT_EQ(opts.allow_hex_numbers, false);
	for (const char * src : {"[0x10]", "[0X10]", "[-0x10]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5Hex, BothParsersAgreeAcrossChunkBoundaries) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_hex_numbers = true;
	struct Case {
		const char * src;
		bool valid;
	};
	const Case cases[] = {
	    {"[0x1F]", true},
	    {"[0xdeadBEEF,1]", true},
	    {"[-0x10]", true},
	    {"{\"a\":0xff}", true},
	    {"[0x]", false},
	    {"[0x1.8]", false},
	};
	for (const Case & c : cases) {
		expect_both(c.src, &opts, c.valid);
	}
}

// ===========================================================================
// A leading plus
// ===========================================================================

TEST(Json5LeadingPlus, NumbersAndTheirValues) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_leading_plus = true;

	struct Case {
		const char * src;
		double dbl;
	};
	const Case cases[] = {
	    {"[+1]", 1.0},
	    {"[+0]", 0.0},
	    {"[+1.5]", 1.5},
	    {"[+1.5e2]", 150.0},
	    {"[+12345]", 12345.0},
	};
	for (const Case & c : cases) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * doc =
		    gtext_json_parse(c.src, std::strlen(c.src), &opts, &err);
		ASSERT_NE(doc, nullptr)
		    << c.src << ": " << (err.message ? err.message : "no message");
		double d = 0.0;
		EXPECT_EQ(gtext_json_get_double(only_number(doc), &d), GTEXT_JSON_OK)
		    << c.src;
		EXPECT_EQ(d, c.dbl) << c.src;
		gtext_json_free(doc);
		gtext_json_error_free(&err);
	}
}

/* The integer accessors have to see through the sign too, or a caller that
   asks for an int64 gets "no such representation" for a whole number. */
TEST(Json5LeadingPlus, TheIntegerAccessorsSeeThroughIt) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_leading_plus = true;

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	const char * src = "[+42]";
	GTEXT_JSON_Value * doc =
	    gtext_json_parse(src, std::strlen(src), &opts, &err);
	ASSERT_NE(doc, nullptr);
	const GTEXT_JSON_Value * n = only_number(doc);
	int64_t i64 = 0;
	EXPECT_EQ(gtext_json_get_i64(n, &i64), GTEXT_JSON_OK);
	EXPECT_EQ(i64, 42);
	uint64_t u64 = 0;
	EXPECT_EQ(gtext_json_get_u64(n, &u64), GTEXT_JSON_OK);
	EXPECT_EQ(u64, 42u);
	gtext_json_free(doc);
	gtext_json_error_free(&err);
}

TEST(Json5LeadingPlus, RefusedWhenTheOptionIsOff) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	EXPECT_EQ(opts.allow_leading_plus, false);
	for (const char * src : {"[+1]", "[+1.5]", "[+0]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5LeadingPlus, ASignAloneIsStillNotANumber) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_leading_plus = true;
	for (const char * src : {"[+]", "[-]", "[+.]", "[++1]", "[+-1]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

/* JSON5's grammar puts the sign in front of the whole value, Infinity and NaN
   included. The sign is this option's; the word is allow_nonfinite_numbers'. */
TEST(Json5LeadingPlus, SignedNonfiniteNeedsBothOptions) {
	GTEXT_JSON_Parse_Options plus_only = number_opts();
	plus_only.allow_leading_plus = true;
	EXPECT_NE(dom_status("[+Infinity]", &plus_only), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[+NaN]", &plus_only), GTEXT_JSON_OK);

	GTEXT_JSON_Parse_Options nonfinite_only = number_opts();
	nonfinite_only.allow_nonfinite_numbers = true;
	EXPECT_NE(dom_status("[+Infinity]", &nonfinite_only), GTEXT_JSON_OK);
	// -Infinity and -NaN need no plus: the sign they carry is a minus.
	EXPECT_EQ(dom_status("[-Infinity]", &nonfinite_only), GTEXT_JSON_OK);
	EXPECT_EQ(dom_status("[-NaN]", &nonfinite_only), GTEXT_JSON_OK);

	GTEXT_JSON_Parse_Options both = number_opts();
	both.allow_leading_plus = true;
	both.allow_nonfinite_numbers = true;
	EXPECT_EQ(dom_status("[+Infinity]", &both), GTEXT_JSON_OK);
	EXPECT_EQ(dom_status("[+NaN]", &both), GTEXT_JSON_OK);
}

TEST(Json5LeadingPlus, SignedInfinityKeepsItsSign) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_leading_plus = true;
	opts.allow_nonfinite_numbers = true;

	struct Case {
		const char * src;
		int positive_inf;
		int negative_inf;
		int nan;
	};
	const Case cases[] = {
	    {"[+Infinity]", 1, 0, 0},
	    {"[-Infinity]", 0, 1, 0},
	    {"[Infinity]", 1, 0, 0},
	    {"[+NaN]", 0, 0, 1},
	    {"[-NaN]", 0, 0, 1},
	    {"[NaN]", 0, 0, 1},
	};
	for (const Case & c : cases) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * doc =
		    gtext_json_parse(c.src, std::strlen(c.src), &opts, &err);
		ASSERT_NE(doc, nullptr)
		    << c.src << ": " << (err.message ? err.message : "no message");
		double d = 0.0;
		ASSERT_EQ(gtext_json_get_double(only_number(doc), &d), GTEXT_JSON_OK)
		    << c.src;
		if (c.nan) {
			EXPECT_TRUE(std::isnan(d)) << c.src;
		}
		else if (c.positive_inf) {
			EXPECT_TRUE(std::isinf(d)) << c.src;
			EXPECT_GT(d, 0.0) << c.src;
		}
		else {
			EXPECT_TRUE(std::isinf(d)) << c.src;
			EXPECT_LT(d, 0.0) << c.src;
		}
		gtext_json_free(doc);
		gtext_json_error_free(&err);
	}
}

TEST(Json5LeadingPlus, BothParsersAgreeAcrossChunkBoundaries) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_leading_plus = true;
	opts.allow_nonfinite_numbers = true;
	struct Case {
		const char * src;
		bool valid;
	};
	const Case cases[] = {
	    {"[+1]", true},
	    {"[+1.5e2,+2]", true},
	    {"[+Infinity]", true},
	    {"[+NaN]", true},
	    {"[-Infinity]", true},
	    {"[-NaN]", true},
	    {"[-NaNx]", false},
	    {"{\"a\":+7}", true},
	    {"[+]", false},
	    {"[+Infin]", false},
	};
	for (const Case & c : cases) {
		expect_both(c.src, &opts, c.valid);
	}
}

// ===========================================================================
// A decimal point at an edge
// ===========================================================================

TEST(Json5BarePoint, BothEdgesAndTheirValues) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_bare_decimal_point = true;

	struct Case {
		const char * src;
		double dbl;
	};
	const Case cases[] = {
	    {"[.5]", 0.5},
	    {"[5.]", 5.0},
	    {"[.5e2]", 50.0},
	    {"[5.e2]", 500.0},
	    {"[.0]", 0.0},
	    {"[0.]", 0.0},
	    {"[-.5]", -0.5},
	    {"[-5.]", -5.0},
	};
	for (const Case & c : cases) {
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * doc =
		    gtext_json_parse(c.src, std::strlen(c.src), &opts, &err);
		ASSERT_NE(doc, nullptr)
		    << c.src << ": " << (err.message ? err.message : "no message");
		double d = 0.0;
		EXPECT_EQ(gtext_json_get_double(only_number(doc), &d), GTEXT_JSON_OK)
		    << c.src;
		EXPECT_EQ(d, c.dbl) << c.src;
		gtext_json_free(doc);
		gtext_json_error_free(&err);
	}
}

/* A document that is nothing but the number, so the token ends at the end of
   the buffer rather than at a bracket. That is a different path in the lexer:
   a number at EOF is where it decides whether more input could still arrive. */
TEST(Json5BarePoint, AtTheEndOfTheDocument) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_bare_decimal_point = true;
	for (const char * src : {".5", "5.", ".5e2"}) {
		EXPECT_EQ(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
	// And with the option off, the same inputs are not numbers.
	GTEXT_JSON_Parse_Options strict = number_opts();
	for (const char * src : {".5", "5."}) {
		EXPECT_NE(dom_status(src, &strict), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5BarePoint, RefusedWhenTheOptionIsOff) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	EXPECT_EQ(opts.allow_bare_decimal_point, false);
	for (const char * src : {"[.5]", "[5.]", "[-.5]", "[.5e2]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

/* The point may sit at an edge; it may not be the whole number. Digits on
   neither side is not a number under any setting. */
TEST(Json5BarePoint, APointAloneIsNotANumber) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_bare_decimal_point = true;
	for (const char * src : {"[.]", "[.e1]", "[..5]", "[5..]", "[-.]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5BarePoint, BothParsersAgreeAcrossChunkBoundaries) {
	GTEXT_JSON_Parse_Options opts = number_opts();
	opts.allow_bare_decimal_point = true;
	struct Case {
		const char * src;
		bool valid;
	};
	const Case cases[] = {
	    {"[.5]", true},
	    {"[5.]", true},
	    {"[.5,5.]", true},
	    {"{\"a\":.5}", true},
	    {"[.]", false},
	    {"[.e1]", false},
	};
	for (const Case & c : cases) {
		expect_both(c.src, &opts, c.valid);
	}
}

// ===========================================================================
// The options are independent
// ===========================================================================

/* Each option admits its own form and nothing else. Turning on hex must not
   quietly bring the plus with it, which is what a single "json5 mode" flag
   inside the lexer would have done. */
TEST(Json5Numbers, EachOptionAdmitsOnlyItsOwnForm) {
	GTEXT_JSON_Parse_Options hex = number_opts();
	hex.allow_hex_numbers = true;
	EXPECT_EQ(dom_status("[0x10]", &hex), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[+1]", &hex), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[.5]", &hex), GTEXT_JSON_OK);

	GTEXT_JSON_Parse_Options plus = number_opts();
	plus.allow_leading_plus = true;
	EXPECT_EQ(dom_status("[+1]", &plus), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[0x10]", &plus), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[.5]", &plus), GTEXT_JSON_OK);

	GTEXT_JSON_Parse_Options point = number_opts();
	point.allow_bare_decimal_point = true;
	EXPECT_EQ(dom_status("[.5]", &point), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[0x10]", &point), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[+1]", &point), GTEXT_JSON_OK);
}

/* And composed: a signed hex literal needs both the sign and the radix. */
TEST(Json5Numbers, PlusAndHexCompose) {
	GTEXT_JSON_Parse_Options both = number_opts();
	both.allow_hex_numbers = true;
	both.allow_leading_plus = true;

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	const char * src = "[+0x10]";
	GTEXT_JSON_Value * doc =
	    gtext_json_parse(src, std::strlen(src), &both, &err);
	ASSERT_NE(doc, nullptr)
	    << (err.message ? err.message : "no message");
	int64_t i64 = 0;
	ASSERT_EQ(gtext_json_get_i64(only_number(doc), &i64), GTEXT_JSON_OK);
	EXPECT_EQ(i64, 16);
	gtext_json_free(doc);
	gtext_json_error_free(&err);

	GTEXT_JSON_Parse_Options hex_only = number_opts();
	hex_only.allow_hex_numbers = true;
	EXPECT_NE(dom_status("[+0x10]", &hex_only), GTEXT_JSON_OK);
}

// ===========================================================================
// ECMAScript string escapes
// ===========================================================================

namespace {

// The decoded bytes of the one string in a single-element array.
std::string only_string(const std::string & src,
    const GTEXT_JSON_Parse_Options * opts, bool * ok) {
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * doc = gtext_json_parse(src.data(), src.size(), opts, &err);
	std::string out;
	*ok = false;
	if (doc) {
		const GTEXT_JSON_Value * v = gtext_json_array_get(doc, 0);
		const char * s = nullptr;
		size_t len = 0;
		if (v && gtext_json_get_string(v, &s, &len) == GTEXT_JSON_OK) {
			out.assign(s, len);
			*ok = true;
		}
		gtext_json_free(doc);
	}
	gtext_json_error_free(&err);
	return out;
}

GTEXT_JSON_Parse_Options escape_opts(void) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.allow_ecma_escapes = true;
	return opts;
}

} // namespace

TEST(Json5Escapes, HexEscapeIsACodepointNotAByte) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	struct Case {
		const char * src;
		const char * expect;
		size_t expect_len;
	};
	// \xe9 is U+00E9, whose UTF-8 is two bytes. A decoder that wrote the byte
	// 0xE9 would produce a string that is not UTF-8 at all, and the validator
	// would then reject a document JSON5 says is valid.
	const Case cases[] = {
	    {"[\"\\x41\"]", "A", 1},
	    {"[\"\\x7F\"]", "\x7F", 1},
	    {"[\"\\xe9\"]", "\xC3\xA9", 2},
	    {"[\"\\xE9\"]", "\xC3\xA9", 2},
	    {"[\"\\xff\"]", "\xC3\xBF", 2},
	    // The length is given rather than measured: this one decodes to a NUL,
	    // which strlen() would read as an empty string.
	    {"[\"\\x00\"]", "\0", 1},
	};
	for (const Case & c : cases) {
		bool ok = false;
		std::string got = only_string(c.src, &opts, &ok);
		EXPECT_TRUE(ok) << c.src;
		EXPECT_EQ(got, std::string(c.expect, c.expect_len)) << c.src;
	}
}

TEST(Json5Escapes, HexEscapeNeedsTwoDigits) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	for (const char * src : {"[\"\\x\"]", "[\"\\x4\"]", "[\"\\xg0\"]",
	         "[\"\\x4g\"]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5Escapes, VerticalTabAndNul) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	bool ok = false;
	EXPECT_EQ(only_string("[\"a\\vb\"]", &opts, &ok), std::string("a\vb"));
	EXPECT_TRUE(ok);
	EXPECT_EQ(only_string("[\"a\\0b\"]", &opts, &ok),
	    std::string("a\0b", 3));
	EXPECT_TRUE(ok);
	// The length is what says the NUL is in there, not the terminator.
	EXPECT_EQ(only_string("[\"\\0\"]", &opts, &ok).size(), 1u);
}

/* \0 is a character; \01 would be an octal escape in a language that no longer
   has them, and \1 through \9 never were anything else. */
TEST(Json5Escapes, OctalIsRefused) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	for (const char * src : {"[\"\\01\"]", "[\"\\09\"]", "[\"\\1\"]",
	         "[\"\\7\"]", "[\"\\9\"]", "[\"\\12\"]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

TEST(Json5Escapes, AnyOtherCharacterEscapesAsItself) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	struct Case {
		const char * src;
		const char * expect;
	};
	const Case cases[] = {
	    {"[\"\\a\"]", "a"},
	    {"[\"\\A\"]", "A"},
	    {"[\"\\'\"]", "'"},
	    {"[\"\\ \"]", " "},
	    {"[\"\\%\"]", "%"},
	    // A multi-byte character escapes as itself in full: U+00E9 here.
	    {"[\"\\\xC3\xA9\"]", "\xC3\xA9"},
	    // And the escapes JSON already had keep their meanings.
	    {"[\"\\n\"]", "\n"},
	    {"[\"\\t\"]", "\t"},
	    {"[\"\\\\\"]", "\\"},
	    {"[\"\\/\"]", "/"},
	};
	for (const Case & c : cases) {
		bool ok = false;
		std::string got = only_string(c.src, &opts, &ok);
		EXPECT_TRUE(ok) << c.src;
		EXPECT_EQ(got, std::string(c.expect)) << c.src;
	}
}

TEST(Json5Escapes, RefusedWhenTheOptionIsOff) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	EXPECT_EQ(opts.allow_ecma_escapes, false);
	for (const char * src : {"[\"\\x41\"]", "[\"\\v\"]", "[\"\\0\"]",
	         "[\"\\a\"]", "[\"\\'\"]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

/* A backslash before a line terminator is a continuation, which is a separate
   option. Without it, the escape is an error rather than quietly meaning a
   newline - which is what the "any other character" rule would have said. */
TEST(Json5Escapes, ALineTerminatorIsNotAnIdentityEscape) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	EXPECT_EQ(opts.allow_line_continuations, false);
	for (const char * src : {"[\"a\\\nb\"]", "[\"a\\\rb\"]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

// ===========================================================================
// Line continuations
// ===========================================================================

TEST(Json5LineContinuation, EveryTerminatorSequence) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.allow_line_continuations = true;

	struct Case {
		const char * src;
		const char * expect;
	};
	const Case cases[] = {
	    {"[\"a\\\nb\"]", "ab"},
	    {"[\"a\\\rb\"]", "ab"},
	    // CRLF is one terminator: if only the CR were swallowed, the LF would
	    // be left as an unescaped control character and the parse would fail.
	    {"[\"a\\\r\nb\"]", "ab"},
	    {"[\"a\\\xE2\x80\xA8" "b\"]", "ab"}, // U+2028
	    {"[\"a\\\xE2\x80\xA9" "b\"]", "ab"}, // U+2029
	    {"[\"a\\\n\\\nb\"]", "ab"},
	    {"[\"\\\n\"]", ""},
	};
	for (const Case & c : cases) {
		bool ok = false;
		std::string got = only_string(c.src, &opts, &ok);
		EXPECT_TRUE(ok) << c.src << ": parse failed";
		EXPECT_EQ(got, std::string(c.expect)) << c.src;
	}
}

TEST(Json5LineContinuation, RefusedWhenTheOptionIsOff) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	for (const char * src : {"[\"a\\\nb\"]", "[\"a\\\r\nb\"]"}) {
		EXPECT_NE(dom_status(src, &opts), GTEXT_JSON_OK) << src;
	}
}

/* A raw newline inside a string is still a control character. The continuation
   is the backslash's doing, not the newline's. */
TEST(Json5LineContinuation, ARawNewlineIsStillRefused) {
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.allow_line_continuations = true;
	EXPECT_NE(dom_status("[\"a\nb\"]", &opts), GTEXT_JSON_OK);
	EXPECT_NE(dom_status("[\"a\rb\"]", &opts), GTEXT_JSON_OK);
}

TEST(Json5Escapes, BothParsersAgreeAcrossChunkBoundaries) {
	GTEXT_JSON_Parse_Options opts = escape_opts();
	opts.allow_line_continuations = true;
	struct Case {
		const char * src;
		bool valid;
	};
	const Case cases[] = {
	    {"[\"\\x41\"]", true},
	    {"[\"a\\vb\"]", true},
	    {"[\"a\\\r\nb\"]", true},
	    {"[\"\\a\"]", true},
	    {"{\"k\":\"\\x41\"}", true},
	    {"[\"\\x4\"]", false},
	    {"[\"\\1\"]", false},
	};
	for (const Case & c : cases) {
		expect_both(c.src, &opts, c.valid);
	}
}
