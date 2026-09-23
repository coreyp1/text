/**
 * @file
 *
 * The pull-model JSON reader.
 *
 * json_stream.h says its events are "valid only for the duration of the
 * callback invocation", so the one thing a wrapper around it must get right is
 * the lifetime of the bytes it hands on.  That is what most of these tests are
 * about, together with the three distinguishable "no event right now" answers a
 * streaming loop needs to tell apart.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include <ghoti.io/text/json.h>

namespace {

struct Seen {
	GTEXT_JSON_Event_Type type;
	std::string text;
	bool boolean;
};

std::string event_text(const GTEXT_JSON_Event & ev) {
	switch (ev.type) {
		case GTEXT_JSON_EVT_STRING:
		case GTEXT_JSON_EVT_KEY:
			return ev.as.str.s ? std::string(ev.as.str.s, ev.as.str.len)
			                   : std::string();
		case GTEXT_JSON_EVT_NUMBER:
			return ev.as.number.s
			           ? std::string(ev.as.number.s, ev.as.number.len)
			           : std::string();
		default:
			return std::string();
	}
}

GTEXT_JSON_Status drain(GTEXT_JSON_Reader * r, std::vector<Seen> & out) {
	for (;;) {
		GTEXT_JSON_Event ev;
		std::memset(&ev, 0, sizeof(ev));
		GTEXT_JSON_Status st = gtext_json_reader_next(r, &ev);
		if (st != GTEXT_JSON_OK) {
			return st;
		}
		out.push_back({ev.type, event_text(ev),
		    ev.type == GTEXT_JSON_EVT_BOOL ? ev.as.boolean : false});
	}
}

} // namespace

TEST(JsonPullReader, ReadsADocumentEventByEvent) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	const char * src = "{\"a\":1,\"b\":[true,null,\"x\"]}";
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_json_reader_feed(r, src, std::strlen(src), &err),
	    GTEXT_JSON_OK)
	    << (err.message ? err.message : "feed failed");
	ASSERT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);

	std::vector<Seen> seen;
	EXPECT_EQ(drain(r, seen), GTEXT_JSON_E_STATE);

	std::vector<GTEXT_JSON_Event_Type> types;
	for (const Seen & s : seen) {
		types.push_back(s.type);
	}
	EXPECT_EQ(types,
	    (std::vector<GTEXT_JSON_Event_Type>{GTEXT_JSON_EVT_OBJECT_BEGIN,
	        GTEXT_JSON_EVT_KEY, GTEXT_JSON_EVT_NUMBER, GTEXT_JSON_EVT_KEY,
	        GTEXT_JSON_EVT_ARRAY_BEGIN, GTEXT_JSON_EVT_BOOL,
	        GTEXT_JSON_EVT_NULL, GTEXT_JSON_EVT_STRING,
	        GTEXT_JSON_EVT_ARRAY_END, GTEXT_JSON_EVT_OBJECT_END}));

	EXPECT_EQ(seen[1].text, "a");
	EXPECT_EQ(seen[2].text, "1");
	EXPECT_EQ(seen[3].text, "b");
	EXPECT_TRUE(seen[5].boolean);
	EXPECT_EQ(seen[7].text, "x");

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

/*
 * The property the copy exists for. The chunk the events came from is
 * overwritten in place after feeding, and every event must still read
 * correctly - both the strings and the number lexemes, which live in different
 * union members and so are two separate chances to get this wrong.
 */
TEST(JsonPullReader, EventBytesSurviveTheChunkTheyCameFrom) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	char chunk[] = "{\"key\":\"value\",\"num\":12345}";
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_json_reader_feed(r, chunk, std::strlen(chunk), &err),
	    GTEXT_JSON_OK);
	ASSERT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);

	std::memset(chunk, 'X', sizeof(chunk) - 1);

	std::vector<Seen> seen;
	drain(r, seen);
	std::vector<std::string> text;
	for (const Seen & s : seen) {
		if (!s.text.empty()) {
			text.push_back(s.text);
		}
	}
	EXPECT_EQ(text,
	    (std::vector<std::string>{"key", "value", "num", "12345"}))
	    << "the reader handed back the caller's overwritten buffer";

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

/* Both string-bearing union members must be copied, not just one. A reader that
   copied `str` and aliased `number` would pass the test above for the string and
   fail for the lexeme, so they are asserted apart. */
TEST(JsonPullReader, NumberLexemesAreCopiedTooNotJustStrings) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	char chunk[] = "[123456789012345678901234567890,1.5e10,-0]";
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_json_reader_feed(r, chunk, std::strlen(chunk), &err),
	    GTEXT_JSON_OK);
	ASSERT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);
	std::memset(chunk, 'X', sizeof(chunk) - 1);

	std::vector<Seen> seen;
	drain(r, seen);
	std::vector<std::string> nums;
	for (const Seen & s : seen) {
		if (s.type == GTEXT_JSON_EVT_NUMBER) {
			nums.push_back(s.text);
		}
	}
	EXPECT_EQ(nums,
	    (std::vector<std::string>{"123456789012345678901234567890", "1.5e10",
	        "-0"}))
	    << "the preserved lexeme must survive its chunk";

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

TEST(JsonPullReader, IncompleteAndEndAreDifferentAnswers) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	GTEXT_JSON_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	EXPECT_EQ(gtext_json_reader_next(r, &ev), GTEXT_JSON_E_INCOMPLETE)
	    << "nothing fed yet";

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_json_reader_feed(r, "{\"a\":", 5, &err), GTEXT_JSON_OK);

	std::vector<Seen> seen;
	EXPECT_EQ(drain(r, seen), GTEXT_JSON_E_INCOMPLETE)
	    << "a half document is still 'not yet'";
	EXPECT_FALSE(seen.empty()) << "the events so far should have arrived";

	ASSERT_EQ(gtext_json_reader_feed(r, "1}", 2, &err), GTEXT_JSON_OK);
	ASSERT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);
	EXPECT_EQ(drain(r, seen), GTEXT_JSON_E_STATE);

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

/* Chunk invariance of the reader itself: the queue is new code and could drop or
   duplicate an event at a feed boundary. */
TEST(JsonPullReader, ByteAtATimeMatchesAllAtOnce) {
	const std::string src =
	    "{\"a\":[1,2,{\"b\":\"deep\\u00e9\"}],\"c\":true,\"d\":null,"
	    "\"e\":1.5e-3,\"f\":\"\",\"g\":{}}";

	auto read_all = [&](size_t chunk) {
		std::vector<Seen> seen;
		GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
		EXPECT_NE(r, nullptr);
		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		for (size_t i = 0; i < src.size(); i += chunk) {
			size_t n = std::min(chunk, src.size() - i);
			EXPECT_EQ(gtext_json_reader_feed(r, src.data() + i, n, &err),
			    GTEXT_JSON_OK)
			    << (err.message ? err.message : "") << " at " << i;
			drain(r, seen);
		}
		EXPECT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);
		drain(r, seen);
		gtext_json_reader_free(r);
		gtext_json_error_free(&err);
		return seen;
	};

	std::vector<Seen> whole = read_all(src.size());
	ASSERT_FALSE(whole.empty());
	for (size_t chunk : {(size_t)1, (size_t)2, (size_t)3, (size_t)5,
	         (size_t)11}) {
		std::vector<Seen> split = read_all(chunk);
		ASSERT_EQ(split.size(), whole.size()) << "chunk size " << chunk;
		for (size_t i = 0; i < whole.size(); i++) {
			EXPECT_EQ(split[i].type, whole[i].type) << chunk << " event " << i;
			EXPECT_EQ(split[i].text, whole[i].text) << chunk << " event " << i;
		}
	}
}

/* An empty string is a value, and must not be spelled with a NULL pointer. */
TEST(JsonPullReader, AnEmptyStringStillCarriesAPointer) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	const char * src = "[\"\",\"\"]";
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_json_reader_feed(r, src, std::strlen(src), &err),
	    GTEXT_JSON_OK);
	ASSERT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);

	size_t strings = 0;
	for (;;) {
		GTEXT_JSON_Event ev;
		std::memset(&ev, 0, sizeof(ev));
		if (gtext_json_reader_next(r, &ev) != GTEXT_JSON_OK) {
			break;
		}
		if (ev.type == GTEXT_JSON_EVT_STRING) {
			strings++;
			EXPECT_NE(ev.as.str.s, nullptr) << "empty string with no pointer";
			EXPECT_EQ(ev.as.str.len, 0u);
		}
	}
	EXPECT_EQ(strings, 2u);

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

TEST(JsonPullReader, AFailureIsRepeatedRatherThanForgotten) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	const char * bad = "{\"a\":tru}";
	// Where the refusal surfaces is the parser's business, not this test's: a
	// bad literal may be caught on the feed or held until the finish. Take
	// whichever reported it, and then require that answer to be repeated.
	GTEXT_JSON_Status fed =
	    gtext_json_reader_feed(r, bad, std::strlen(bad), &err);
	if (fed == GTEXT_JSON_OK) {
		fed = gtext_json_reader_feed(r, nullptr, 0, &err);
	}
	ASSERT_NE(fed, GTEXT_JSON_OK) << "a bad literal must be refused";

	GTEXT_JSON_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	GTEXT_JSON_Status last = GTEXT_JSON_OK;
	for (int i = 0; i < 16; i++) {
		last = gtext_json_reader_next(r, &ev);
		if (last != GTEXT_JSON_OK) {
			break;
		}
	}
	EXPECT_EQ(last, fed);
	EXPECT_EQ(gtext_json_reader_next(r, &ev), fed) << "and again";
	EXPECT_EQ(gtext_json_reader_feed(r, "1", 1, &err), fed)
	    << "and must not start over on more input";

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

TEST(JsonPullReader, EndOfInputTwiceIsFineAndBytesAfterItAreNot) {
	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));

	ASSERT_EQ(gtext_json_reader_feed(r, "[1]", 3, &err), GTEXT_JSON_OK);
	EXPECT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);
	EXPECT_EQ(gtext_json_reader_feed(r, nullptr, 0, &err), GTEXT_JSON_OK);
	EXPECT_EQ(gtext_json_reader_feed(r, "[2]", 3, &err), GTEXT_JSON_E_STATE);

	gtext_json_reader_free(r);
	gtext_json_error_free(&err);
}

TEST(JsonPullReader, NullArgumentsAreRefusedRatherThanCrashing) {
	GTEXT_JSON_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	EXPECT_EQ(gtext_json_reader_next(nullptr, &ev), GTEXT_JSON_E_INVALID);
	EXPECT_EQ(gtext_json_reader_feed(nullptr, "a", 1, nullptr),
	    GTEXT_JSON_E_INVALID);
	gtext_json_reader_free(nullptr); // no-op

	GTEXT_JSON_Reader * r = gtext_json_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	EXPECT_EQ(gtext_json_reader_next(r, nullptr), GTEXT_JSON_E_INVALID);
	EXPECT_EQ(gtext_json_reader_feed(r, nullptr, 5, nullptr),
	    GTEXT_JSON_E_INVALID);
	gtext_json_reader_free(r);
}
