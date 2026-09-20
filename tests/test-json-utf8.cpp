/**
 * Well-formed UTF-8, and a failure that says so.
 *
 * Two defects, found by scoring this parser against JSONTestSuite for the
 * first time.
 *
 * The validator checked the shape of a sequence and never the value it
 * encoded, so every ill-formed encoding RFC 3629 section 3 exists to forbid
 * parsed as a string with validate_utf8 on, its default:
 *
 *   C0 AF        an overlong "/", the classic way past a filter that looks
 *                for the character rather than the bytes
 *   E0 80 80     an overlong NUL
 *   ED A0 80     U+D800, a surrogate half, which UTF-8 cannot encode
 *   F4 BF BF BF  U+13FFFF, past the last code point
 *   F5 80 80 80  a leading byte that is never valid
 *
 * The YAML scanner's validator had the value checks from the start. This is
 * the same rule where the JSON string decoder can reach it.
 *
 * The second is what a caller was told. Some failures returned NULL from
 * gtext_json_parse() with the error struct untouched - code still
 * GTEXT_JSON_OK and no message - so a caller testing the code rather than the
 * pointer read a refusal as a success. A leading-zero number and these UTF-8
 * sequences both arrived that way.
 *
 * Note the gtext_json_error_free() after every parse. A refused parse may
 * leave a heap-allocated context_snippet behind, and the caller owns it - the
 * first version of this file did not, and ASan reported 30 bytes leaked
 * across five allocations. The leak was the test's, not the library's, but it
 * is the same mistake a user would make from the same reading.
 */
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <ghoti.io/text/json.h>
#include <string.h>
}

namespace {

/* A JSON array holding one string made of the given raw bytes. */
std::string Wrap(const std::string &raw) {
	return "[\"" + raw + "\"]";
}

std::string Bytes(std::initializer_list<int> bs) {
	std::string s;
	for (int b : bs) s.push_back(static_cast<char>(b));
	return s;
}

struct Case {
	const char *name;
	std::string raw;
};

const Case kIllFormed[] = {
	{"overlong 2-byte solidus",   Bytes({0xC0, 0xAF})},
	{"overlong 2-byte",           Bytes({0xC1, 0xBF})},
	{"overlong 3-byte NUL",       Bytes({0xE0, 0x80, 0x80})},
	{"surrogate U+D800",          Bytes({0xED, 0xA0, 0x80})},
	{"surrogate U+DFFF",          Bytes({0xED, 0xBF, 0xBF})},
	{"overlong 4-byte",           Bytes({0xF0, 0x80, 0x80, 0x80})},
	{"beyond U+10FFFF",           Bytes({0xF4, 0xBF, 0xBF, 0xBF})},
	{"leading byte F5",           Bytes({0xF5, 0x80, 0x80, 0x80})},
	{"lone continuation byte",    Bytes({0x80})},
	{"truncated 3-byte",          Bytes({0xE2, 0x82})},
};

const Case kWellFormed[] = {
	{"ASCII",                     "abc"},
	{"2-byte e-acute",            Bytes({0xC3, 0xA9})},
	{"3-byte euro",               Bytes({0xE2, 0x82, 0xAC})},
	{"4-byte emoji",              Bytes({0xF0, 0x9F, 0x98, 0x80})},
	{"U+0080, the 2-byte floor",  Bytes({0xC2, 0x80})},
	{"U+0800, the 3-byte floor",  Bytes({0xE0, 0xA0, 0x80})},
	{"U+10000, the 4-byte floor", Bytes({0xF0, 0x90, 0x80, 0x80})},
	{"U+10FFFF, the last one",    Bytes({0xF4, 0x8F, 0xBF, 0xBF})},
	{"just below the surrogates", Bytes({0xED, 0x9F, 0xBF})},
	{"just above the surrogates", Bytes({0xEE, 0x80, 0x80})},
};

GTEXT_JSON_Value *Parse(const std::string &doc, GTEXT_JSON_Error *err) {
	memset(err, 0, sizeof(*err));
	return gtext_json_parse(doc.data(), doc.size(), nullptr, err);
}

} // namespace

TEST(JsonUtf8, IllFormedSequencesAreRefused) {
	for (const Case &c : kIllFormed) {
		const std::string doc = Wrap(c.raw);
		GTEXT_JSON_Error err;
		GTEXT_JSON_Value *v = Parse(doc, &err);
		EXPECT_EQ(v, nullptr) << "accepted: " << c.name;
		if (v) gtext_json_free(v);
		gtext_json_error_free(&err);
	}
}

TEST(JsonUtf8, WellFormedSequencesAreAccepted) {
	for (const Case &c : kWellFormed) {
		const std::string doc = Wrap(c.raw);
		GTEXT_JSON_Error err;
		GTEXT_JSON_Value *v = Parse(doc, &err);
		EXPECT_NE(v, nullptr) << "refused: " << c.name
			<< " (" << (err.message ? err.message : "no message") << ")";
		if (v) gtext_json_free(v);
		gtext_json_error_free(&err);
	}
}

/* A refusal has to look like one. Returning NULL while leaving the error
 * struct saying OK is worse than a wrong message: a caller that checks the
 * code sees success. */
TEST(JsonUtf8, ARefusalAlwaysCarriesACodeAndAMessage) {
	const std::string inputs[] = {
		Wrap(Bytes({0xC0, 0xAF})),
		Wrap(Bytes({0xED, 0xA0, 0x80})),
		"[01]",
		"[1e]",
		"\"unterminated",
		"[\"\\x\"]",
		"[1,]",
		"{\"a\":}",
	};
	for (const std::string &doc : inputs) {
		GTEXT_JSON_Error err;
		GTEXT_JSON_Value *v = Parse(doc, &err);
		ASSERT_EQ(v, nullptr) << "expected a refusal";
		EXPECT_NE(err.code, GTEXT_JSON_OK)
			<< "refused but reported GTEXT_JSON_OK";
		EXPECT_NE(err.message, nullptr) << "refused with no message";
		gtext_json_error_free(&err);
	}
}

/* And the message has to be the one closest to the fault. The fallback fills
 * in only where nothing deeper spoke; overwriting a specific message with a
 * generic one would lose the better answer. */
TEST(JsonUtf8, TheMostSpecificMessageIsTheOneKept) {
	struct { const char *doc; const char *wanted; } kCases[] = {
		{"[1,]", "Trailing comma not allowed"},
		{"[1 2]", "Expected comma between array elements"},
		{"{\"a\":}", "Unexpected token"},
	};
	for (const auto &c : kCases) {
		GTEXT_JSON_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value *v =
			gtext_json_parse(c.doc, strlen(c.doc), nullptr, &err);
		ASSERT_EQ(v, nullptr) << "expected a refusal: " << c.doc;
		ASSERT_NE(err.message, nullptr);
		EXPECT_STREQ(err.message, c.wanted) << "input: " << c.doc;
		gtext_json_error_free(&err);
	}
}

/* validate_utf8 is a choice, and turning it off still means what it says. */
TEST(JsonUtf8, ValidationCanBeTurnedOff) {
	const std::string doc = Wrap(Bytes({0xC0, 0xAF}));
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.validate_utf8 = false;
	GTEXT_JSON_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value *v =
		gtext_json_parse(doc.data(), doc.size(), &opts, &err);
	EXPECT_NE(v, nullptr)
		<< "validate_utf8=false still refused it: "
		<< (err.message ? err.message : "");
	if (v) gtext_json_free(v);
	gtext_json_error_free(&err);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
