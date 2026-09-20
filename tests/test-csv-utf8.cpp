/**
 * The edges of UTF-8, in both CSV parsers.
 *
 * Both had the same line wrong, and the comment above the streaming one said
 * so without noticing: it promised the rules "match csv_validate_utf8()
 * exactly", and they did - including the mistake.
 *
 * After a leading F4 the second byte runs 80..8F and no further, because
 * F4 8F BF BF is U+10FFFF, the last code point there is. The test was
 * `(b1 & 0xF0) != 0`, which is true of *every* continuation byte, 0x80
 * included. So the whole of plane 16 - U+100000 to U+10FFFF - was thrown out
 * along with the sequences the test meant to catch, and a CSV field holding
 * any character from there was refused as invalid UTF-8.
 *
 * Nothing caught it. It is a false refusal rather than a false acceptance, so
 * the fuzzer had no reason to care, and no test reached that plane. It turned
 * up while checking whether CSV shared a hole found in the JSON validator by
 * scoring against JSONTestSuite - it did not have that one, and had this
 * instead.
 *
 * The boundaries below are the ones worth writing down: the last code point
 * of each sequence length, the first one past it, and the surrogate gap.
 */
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <ghoti.io/text/csv.h>
#include <string.h>
}

namespace {

std::string Bytes(std::initializer_list<int> bs) {
	std::string s;
	for (int b : bs) s.push_back(static_cast<char>(b));
	return s;
}

struct Case {
	const char *name;
	std::string raw;
};

const Case kWellFormed[] = {
	{"U+0080, first 2-byte",     Bytes({0xC2, 0x80})},
	{"U+07FF, last 2-byte",      Bytes({0xDF, 0xBF})},
	{"U+0800, first 3-byte",     Bytes({0xE0, 0xA0, 0x80})},
	{"U+D7FF, below surrogates", Bytes({0xED, 0x9F, 0xBF})},
	{"U+E000, above surrogates", Bytes({0xEE, 0x80, 0x80})},
	{"U+FFFF, last 3-byte",      Bytes({0xEF, 0xBF, 0xBF})},
	{"U+10000, first 4-byte",    Bytes({0xF0, 0x90, 0x80, 0x80})},
	{"U+100000, plane 16",       Bytes({0xF4, 0x80, 0x80, 0x80})},
	{"U+10FFFF, the last one",   Bytes({0xF4, 0x8F, 0xBF, 0xBF})},
};

const Case kIllFormed[] = {
	{"U+110000, one past the end", Bytes({0xF4, 0x90, 0x80, 0x80})},
	{"leading byte F5",            Bytes({0xF5, 0x80, 0x80, 0x80})},
	{"overlong 2-byte solidus",    Bytes({0xC0, 0xAF})},
	{"overlong 3-byte",            Bytes({0xE0, 0x80, 0x80})},
	{"overlong 4-byte",            Bytes({0xF0, 0x80, 0x80, 0x80})},
	{"surrogate U+D800",           Bytes({0xED, 0xA0, 0x80})},
	{"surrogate U+DFFF",           Bytes({0xED, 0xBF, 0xBF})},
	{"lone continuation byte",     Bytes({0x80})},
};

std::string Row(const std::string &raw) { return "a," + raw + "\n"; }

bool TableAccepts(const std::string &doc) {
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table *t =
		gtext_csv_parse_table(doc.data(), doc.size(), &opts, &err);
	if (!t) return false;
	gtext_csv_free_table(t);
	return true;
}

GTEXT_CSV_Status NoopCallback(const GTEXT_CSV_Event *e, void *user) {
	(void)e; (void)user;
	return GTEXT_CSV_OK;
}

/* The streaming parser, fed in `chunk`-byte pieces (0 meaning all at once). */
bool StreamAccepts(const std::string &doc, size_t chunk) {
	GTEXT_CSV_Stream *s = gtext_csv_stream_new(nullptr, NoopCallback, nullptr);
	if (!s) return false;
	bool ok = true;
	size_t off = 0;
	while (off < doc.size()) {
		const size_t n = chunk ? std::min(chunk, doc.size() - off)
		                       : doc.size() - off;
		if (gtext_csv_stream_feed(s, doc.data() + off, n, nullptr)
				!= GTEXT_CSV_OK) {
			ok = false;
			break;
		}
		off += n;
	}
	if (ok && gtext_csv_stream_finish(s, nullptr) != GTEXT_CSV_OK) ok = false;
	gtext_csv_stream_free(s);
	return ok;
}

} // namespace

TEST(CsvUtf8, TheTableParserTakesEveryValidCodePoint) {
	for (const Case &c : kWellFormed) {
		EXPECT_TRUE(TableAccepts(Row(c.raw))) << "refused: " << c.name;
	}
}

TEST(CsvUtf8, TheTableParserRefusesIllFormedSequences) {
	for (const Case &c : kIllFormed) {
		EXPECT_FALSE(TableAccepts(Row(c.raw))) << "accepted: " << c.name;
	}
}

/* The two parsers have to agree, which is what the comment in csv_stream.c
 * claims and what stopped anyone looking at either. */
TEST(CsvUtf8, TheStreamingParserAgreesWithTheTableParser) {
	for (const Case *set : {kWellFormed, kIllFormed}) {
		const size_t n = (set == kWellFormed)
			? sizeof(kWellFormed) / sizeof(kWellFormed[0])
			: sizeof(kIllFormed) / sizeof(kIllFormed[0]);
		for (size_t i = 0; i < n; i++) {
			const std::string doc = Row(set[i].raw);
			const bool table = TableAccepts(doc);
			EXPECT_EQ(StreamAccepts(doc, 0), table)
				<< "whole-input stream disagrees: " << set[i].name;
			EXPECT_EQ(StreamAccepts(doc, 1), table)
				<< "byte-by-byte stream disagrees: " << set[i].name;
		}
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
