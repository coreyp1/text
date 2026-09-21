/**
 * @file
 *
 * Internationalized host names, RFC 5890 to 5893.
 *
 * These go at the internal function rather than through JSON Schema's
 * `format`, because the rules being checked are IDNA's and not the schema
 * engine's, and a failure here should say so.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "../src/idna/idna_internal.h"
}

static bool host(const std::string & s) {
	return gtext_idna_hostname_valid(s.data(), s.size(), 0) != 0;
}

static bool idn(const std::string & s) {
	return gtext_idna_hostname_valid(s.data(), s.size(), 1) != 0;
}

TEST(Idna, AsciiHostNames) {
	EXPECT_TRUE(host("www.example.com"));
	EXPECT_TRUE(host("host-name"));
	EXPECT_TRUE(host("1host"));
	EXPECT_FALSE(host(""));
	EXPECT_FALSE(host("-hello"));
	EXPECT_FALSE(host("hello-"));
	EXPECT_FALSE(host("a..b"));
	// The rooted form is an empty root label, which is an empty label.
	EXPECT_FALSE(host("example.com."));
	EXPECT_FALSE(host(".example"));
	EXPECT_FALSE(host("not_an_underscore"));
	EXPECT_FALSE(host(std::string(64, 'a')));
	EXPECT_TRUE(host(std::string(63, 'a')));
}

TEST(Idna, ALabelsAreDecodedAndChecked) {
	// 2020-12 defines `hostname` to include Punycode-produced names, so an
	// A-label is decoded even in the ASCII-only mode. Accepting `xn--` and
	// any trailing base-36 is not checking the thing the keyword names.
	EXPECT_TRUE(host("xn--ihqwcrb4cv8a8dqg056pqjye"));
	EXPECT_TRUE(host("xn--nxasmq6b"));
	// The body is case-insensitive, and so is the prefix.
	EXPECT_TRUE(host("xn--NXASMQ6B"));
	EXPECT_FALSE(host("xn--X"));
	// RFC 5891 section 4.4 requires the round trip: this decodes to something
	// whose own encoding is different, so it was never an A-label.
	EXPECT_FALSE(host("xn---9uc"));
	// Decoding to pure ASCII means the label should not have been encoded.
	EXPECT_FALSE(host("xn--example-"));
	// A label that decodes to a disallowed codepoint is not a name.
	EXPECT_FALSE(host("xn--7a"));
	// Reserved by the ACE prefix: a hyphen in the third and fourth positions.
	EXPECT_FALSE(host("XN--aa---o47jg78q"));
}

TEST(Idna, UnicodeLabelsOnlyWhereAllowed) {
	EXPECT_TRUE(idn("\xEC\x8B\xA4\xEB\xA1\x80.\xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8"));
	// The same name is not a `hostname`, which is ASCII.
	EXPECT_FALSE(host("\xEC\x8B\xA4\xEB\xA1\x80"));
	// U+302E HANGUL SINGLE DOT TONE MARK is a DISALLOWED exception.
	EXPECT_FALSE(idn("\xE3\x80\xAE\xEC\x8B\xA4\xEB\xA1\x80"));
	// Leading combining marks.
	EXPECT_FALSE(idn("\xE0\xA4\x83hello"));
	EXPECT_FALSE(idn("\xCC\x80hello"));
}

TEST(Idna, Rfc5892Exceptions) {
	// PVALID exceptions: sharp s, final sigma, tibetan tsheg, ideographic zero.
	EXPECT_TRUE(idn("\xC3\x9F\xCF\x82\xE0\xBC\x8B\xE3\x80\x87"));
	// DISALLOWED exceptions: tatweel and nko lajanyalan.
	EXPECT_FALSE(idn("\xD9\x80\xDF\xBA"));
}

TEST(Idna, ContextualRules) {
	// MIDDLE DOT needs an 'l' on both sides (rule A.3).
	EXPECT_TRUE(idn("l\xC2\xB7l"));
	EXPECT_FALSE(idn("a\xC2\xB7l"));
	EXPECT_FALSE(idn("l\xC2\xB7""a"));
	EXPECT_FALSE(idn("\xC2\xB7l"));
	EXPECT_FALSE(idn("l\xC2\xB7"));

	// GREEK KERAIA must be followed by Greek (rule A.4).
	EXPECT_TRUE(idn("\xCE\xB1\xCD\xB5\xCE\xB2"));
	EXPECT_FALSE(idn("\xCE\xB1\xCD\xB5S"));
	EXPECT_FALSE(idn("\xCE\xB1\xCD\xB5"));

	// HEBREW GERESH and GERSHAYIM must be preceded by Hebrew (A.5, A.6).
	EXPECT_TRUE(idn("\xD7\x90\xD7\xB3\xD7\x91"));
	EXPECT_FALSE(idn("A\xD7\xB3\xD7\x91"));
	EXPECT_TRUE(idn("\xD7\x90\xD7\xB4\xD7\x91"));
	EXPECT_FALSE(idn("A\xD7\xB4\xD7\x91"));

	// KATAKANA MIDDLE DOT needs Hiragana, Katakana or Han somewhere in the
	// label - anywhere, which is why it is the one rule that reads the whole
	// label rather than its neighbours (A.7).
	EXPECT_TRUE(idn("\xE3\x83\xBB\xE3\x81\x81"));
	EXPECT_TRUE(idn("\xE3\x83\xBB\xE4\xB8\x88"));
	EXPECT_FALSE(idn("def\xE3\x83\xBB""abc"));
	EXPECT_FALSE(idn("\xE3\x83\xBB"));

	// The two blocks of Arabic-Indic digits may not mix in one label, and may
	// appear in different labels of the same name (A.8, A.9).
	EXPECT_FALSE(idn("\xD8\xA8\xD9\xA0\xDB\xB0"));
	EXPECT_TRUE(idn("\xD8\xA8\xD9\xA0\xD8\xA8"));
	EXPECT_TRUE(idn("\xD8\xA8\xD9\xA0.\xD8\xA8\xDB\xB0"));
}

TEST(Idna, ZeroWidthJoinersNeedTheirContext) {
	// ZWJ must follow a virama (rule A.2).
	EXPECT_TRUE(idn("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8D\xE0\xA4\xB7"));
	EXPECT_FALSE(idn("\xE0\xA4\x95\xE2\x80\x8D\xE0\xA4\xB7"));
	EXPECT_FALSE(idn("\xE2\x80\x8D\xE0\xA4\xB7"));

	// ZWNJ passes with a virama before it, or with the joining-type pattern
	// around it (rule A.1) - the second is what makes this Arabic name valid.
	EXPECT_TRUE(idn("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8C\xE0\xA4\xB7"));
	EXPECT_TRUE(idn("\xD8\xA8\xD9\x8A\xE2\x80\x8C\xD8\xA8\xD9\x8A"));
	// Every occurrence has to pass, not just the first.
	EXPECT_FALSE(idn("\xE0\xA4\x95\xE0\xA5\x8D\xE2\x80\x8Cx\xE2\x80\x8Cy"));
}

TEST(Idna, TheBidiRuleIsAboutTheWholeName) {
	// One right-to-left label makes the name a bidi domain name, and then
	// every label is judged by RFC 5893 - including the ones that would have
	// been fine on their own.
	EXPECT_FALSE(idn("0a.\xD7\x90"));
	EXPECT_FALSE(idn("0\xD8\xA7"));
	EXPECT_FALSE(idn("a\xD7\x90"));
	// A right-to-left label may not mix the two kinds of digit.
	EXPECT_FALSE(idn("\xD7\x90""0\xD9\xA0"));
	// Without any right-to-left character the rule does not apply.
	EXPECT_TRUE(idn("0a.example"));
}

TEST(Idna, Lengths) {
	// The 63-octet limit is on the A-label form, so a U-label is measured by
	// what it would encode to rather than by its own length.
	// Punycode is compact for a repeated character, so the threshold is not
	// where the character count would suggest: 57 of these encode to exactly
	// 63 octets and 58 to 64.
	std::string u57;
	std::string u58;
	for (int i = 0; i < 58; i++) {
		// LATIN SMALL LETTER U WITH DIAERESIS
		if (i < 57) {
			u57 += "\xC3\xBC";
		}
		u58 += "\xC3\xBC";
	}
	EXPECT_TRUE(idn(u57));
	EXPECT_FALSE(idn(u58));

	std::string long_name;
	for (int i = 0; i < 5; i++) {
		long_name += std::string(63, 'a') + ".";
	}
	long_name += "a";
	EXPECT_FALSE(host(long_name));
}

TEST(Idna, LabelSeparators) {
	// UTS #46 section 4.5: three other stops separate labels, but only for a
	// name that admits Unicode at all.
	EXPECT_TRUE(idn("a\xE3\x80\x82""b"));
	EXPECT_TRUE(idn("a\xEF\xBC\x8E""b"));
	EXPECT_TRUE(idn("a\xEF\xBD\xA1""b"));
	EXPECT_FALSE(idn("\xE3\x80\x82"));
	EXPECT_FALSE(idn("\xE3\x80\x82""example"));
	EXPECT_FALSE(idn("example\xE3\x80\x82"));
	// For a plain `hostname` that is one label with a character no label may
	// contain, which is invalid for a different and truer reason.
	EXPECT_FALSE(host("example\xEF\xBC\x8E""com"));
}

TEST(Idna, MalformedUtf8IsNotAName) {
	EXPECT_FALSE(idn(std::string("a\xC3", 2)));
	EXPECT_FALSE(idn(std::string("\xED\xA0\x80", 3)));  // a surrogate
	EXPECT_FALSE(idn(std::string("\xC0\xAF", 2)));      // overlong
	EXPECT_FALSE(idn(std::string("a\0b", 3)));
}

int main(int argc, char ** argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
