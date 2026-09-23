/**
 * @file
 *
 * The pull-model CSV reader.
 *
 * The push parser calls the caller; the pull reader lets the caller call the
 * parser.  What these tests pin is mostly the difference between the two: the
 * three distinguishable "no event right now" answers, and the lifetime of the
 * bytes an event points at - which is the one property a wrapper around a push
 * parser can get wrong without any test noticing, because a stale pointer stays
 * readable for a while after it stops being valid.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include <ghoti.io/text/csv.h>

namespace {

struct Seen {
	GTEXT_CSV_Event_Type type;
	std::string data;
	size_t row;
	size_t col;
};

// Drain whatever is queued, returning the status that stopped the drain.
GTEXT_CSV_Status drain(GTEXT_CSV_Reader * r, std::vector<Seen> & out) {
	for (;;) {
		GTEXT_CSV_Event ev;
		std::memset(&ev, 0, sizeof(ev));
		GTEXT_CSV_Status st = gtext_csv_reader_next(r, &ev);
		if (st != GTEXT_CSV_OK) {
			return st;
		}
		out.push_back({ev.type,
		    ev.type == GTEXT_CSV_EVENT_FIELD && ev.data
		        ? std::string(ev.data, ev.data_len)
		        : std::string(),
		    ev.row_index, ev.col_index});
	}
}

std::vector<std::string> fields_of(const std::vector<Seen> & seen) {
	std::vector<std::string> out;
	for (const Seen & s : seen) {
		if (s.type == GTEXT_CSV_EVENT_FIELD) {
			out.push_back(s.data);
		}
	}
	return out;
}

} // namespace

TEST(CsvPullReader, ReadsAWholeDocumentARecordAtATime) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	const char * src = "a,b\n1,2\n";
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_csv_reader_feed(r, src, std::strlen(src), &err),
	    GTEXT_CSV_OK)
	    << (err.message ? err.message : "feed failed");
	ASSERT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);

	std::vector<Seen> seen;
	EXPECT_EQ(drain(r, seen), GTEXT_CSV_E_STATE)
	    << "after end of input an empty queue is the end, not 'not yet'";
	EXPECT_EQ(fields_of(seen), (std::vector<std::string>{"a", "b", "1", "2"}));

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/*
 * The property that makes the copy necessary.
 *
 * The push parser's `data` points into the current chunk or into a field buffer
 * it is about to reuse, so a reader that stored the pointer would hand the
 * caller bytes that have since been overwritten. Here the chunk the field came
 * from is overwritten in place after the feed, and the event must still read
 * correctly. Without the copy this reads the scribble - a use-after-free that
 * would otherwise show up only under a sanitizer, if at all.
 */
TEST(CsvPullReader, EventBytesSurviveTheChunkTheyCameFrom) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	char chunk[] = "hello,world\n";
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_csv_reader_feed(r, chunk, std::strlen(chunk), &err),
	    GTEXT_CSV_OK);
	ASSERT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);

	// The caller's buffer is gone as far as the reader is concerned.
	std::memset(chunk, 'X', sizeof(chunk) - 1);

	std::vector<Seen> seen;
	drain(r, seen);
	EXPECT_EQ(fields_of(seen), (std::vector<std::string>{"hello", "world"}))
	    << "the reader handed back the caller's overwritten buffer";

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/* An event's bytes are documented as valid until the *next* call, not until the
   current one returns. A reader that freed on handing out would fail this; one
   that never freed is caught by the allocator test instead. */
TEST(CsvPullReader, AnEventOutlivesTheCallThatReturnedIt) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	const char * src = "first,second\n";
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_csv_reader_feed(r, src, std::strlen(src), &err),
	    GTEXT_CSV_OK);
	ASSERT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);

	GTEXT_CSV_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	const char * held = nullptr;
	size_t held_len = 0;
	while (gtext_csv_reader_next(r, &ev) == GTEXT_CSV_OK) {
		if (ev.type == GTEXT_CSV_EVENT_FIELD) {
			held = ev.data;
			held_len = ev.data_len;
			break;
		}
	}
	ASSERT_NE(held, nullptr);
	EXPECT_EQ(std::string(held, held_len), "first");

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/* Three answers, not two: "not yet" before end of input, "never" after it. A
   reader that returned one status for both would make a streaming loop either
   spin forever or stop early. */
TEST(CsvPullReader, IncompleteAndEndAreDifferentAnswers) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);

	GTEXT_CSV_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	EXPECT_EQ(gtext_csv_reader_next(r, &ev), GTEXT_CSV_E_INCOMPLETE)
	    << "nothing fed yet";

	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	// A partial record: bytes with no terminator, so the parser cannot know
	// the field has ended.
	ASSERT_EQ(gtext_csv_reader_feed(r, "abc", 3, &err), GTEXT_CSV_OK);

	std::vector<Seen> seen;
	EXPECT_EQ(drain(r, seen), GTEXT_CSV_E_INCOMPLETE)
	    << "an unterminated record is still 'not yet'";

	ASSERT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);
	EXPECT_EQ(drain(r, seen), GTEXT_CSV_E_STATE);

	std::vector<std::string> f = fields_of(seen);
	EXPECT_NE(std::find(f.begin(), f.end(), "abc"), f.end())
	    << "finishing must flush the pending field";

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/* Fed one byte at a time, the reader must produce exactly what one feed does.
   Chunk invariance asked of the reader rather than of the parser underneath it:
   the queue is new code and could drop or duplicate an event at a boundary. */
TEST(CsvPullReader, ByteAtATimeMatchesAllAtOnce) {
	const std::string src =
	    "name,note\n\"a,b\",\"multi\nline\"\n,\ntrailing,x\n";

	auto read_all = [&](size_t chunk) {
		std::vector<Seen> seen;
		GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
		EXPECT_NE(r, nullptr);
		GTEXT_CSV_Error err;
		std::memset(&err, 0, sizeof(err));
		for (size_t i = 0; i < src.size(); i += chunk) {
			size_t n = std::min(chunk, src.size() - i);
			EXPECT_EQ(gtext_csv_reader_feed(r, src.data() + i, n, &err),
			    GTEXT_CSV_OK)
			    << (err.message ? err.message : "") << " at " << i;
			drain(r, seen);
		}
		EXPECT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);
		drain(r, seen);
		gtext_csv_reader_free(r);
		gtext_csv_error_free(&err);
		return seen;
	};

	std::vector<Seen> whole = read_all(src.size());
	ASSERT_FALSE(whole.empty());
	for (size_t chunk : {(size_t)1, (size_t)2, (size_t)3, (size_t)7,
	         (size_t)13}) {
		std::vector<Seen> split = read_all(chunk);
		ASSERT_EQ(split.size(), whole.size()) << "chunk size " << chunk;
		for (size_t i = 0; i < whole.size(); i++) {
			EXPECT_EQ(split[i].type, whole[i].type) << chunk << " event " << i;
			EXPECT_EQ(split[i].data, whole[i].data) << chunk << " event " << i;
			EXPECT_EQ(split[i].row, whole[i].row) << chunk << " event " << i;
			EXPECT_EQ(split[i].col, whole[i].col) << chunk << " event " << i;
		}
	}
}

/* An empty field is a field. `data == NULL` must not be how the reader spells
   it, or "empty field" and "not a field event" become the same thing. */
TEST(CsvPullReader, AnEmptyFieldIsStillAField) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	const char * src = "a,,c\n";
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	ASSERT_EQ(gtext_csv_reader_feed(r, src, std::strlen(src), &err),
	    GTEXT_CSV_OK);
	ASSERT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);

	size_t field_events = 0;
	size_t null_data = 0;
	for (;;) {
		GTEXT_CSV_Event ev;
		std::memset(&ev, 0, sizeof(ev));
		if (gtext_csv_reader_next(r, &ev) != GTEXT_CSV_OK) {
			break;
		}
		if (ev.type == GTEXT_CSV_EVENT_FIELD) {
			field_events++;
			if (!ev.data) {
				null_data++;
			}
		}
	}
	EXPECT_EQ(field_events, 3u);
	EXPECT_EQ(null_data, 0u) << "an empty field must still carry a pointer";

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/* A parse failure must keep saying so. If the reader returned "no more events"
   after an error, a loop reading until it stops would treat a broken document
   as a complete one. */
TEST(CsvPullReader, AFailureIsRepeatedRatherThanForgotten) {
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.max_cols = 2; // so a third column is a refusal

	GTEXT_CSV_Reader * r = gtext_csv_reader_new(&opts);
	ASSERT_NE(r, nullptr);

	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	const char * src = "a,b,c,d\n";
	GTEXT_CSV_Status fed =
	    gtext_csv_reader_feed(r, src, std::strlen(src), &err);
	ASSERT_NE(fed, GTEXT_CSV_OK) << "too many columns must be refused";

	GTEXT_CSV_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	GTEXT_CSV_Status last = GTEXT_CSV_OK;
	for (int i = 0; i < 16; i++) {
		last = gtext_csv_reader_next(r, &ev);
		if (last != GTEXT_CSV_OK) {
			break;
		}
	}
	EXPECT_EQ(last, fed) << "the reader must keep reporting the parse error";
	EXPECT_EQ(gtext_csv_reader_next(r, &ev), fed) << "and again";
	EXPECT_EQ(gtext_csv_reader_feed(r, "x\n", 2, &err), fed)
	    << "and must not start over on more input";

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

/* Boundary handling of the end-of-input spelling. */
TEST(CsvPullReader, EndOfInputTwiceIsFineAndBytesAfterItAreNot) {
	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));

	ASSERT_EQ(gtext_csv_reader_feed(r, "a\n", 2, &err), GTEXT_CSV_OK);
	EXPECT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK);
	EXPECT_EQ(gtext_csv_reader_feed(r, nullptr, 0, &err), GTEXT_CSV_OK)
	    << "a loop that finishes twice must not be an error";
	EXPECT_EQ(gtext_csv_reader_feed(r, "b\n", 2, &err), GTEXT_CSV_E_STATE)
	    << "bytes after end of input are the caller's mistake";

	gtext_csv_reader_free(r);
	gtext_csv_error_free(&err);
}

TEST(CsvPullReader, NullArgumentsAreRefusedRatherThanCrashing) {
	GTEXT_CSV_Event ev;
	std::memset(&ev, 0, sizeof(ev));
	EXPECT_EQ(gtext_csv_reader_next(nullptr, &ev), GTEXT_CSV_E_INVALID);
	EXPECT_EQ(gtext_csv_reader_feed(nullptr, "a", 1, nullptr),
	    GTEXT_CSV_E_INVALID);
	gtext_csv_reader_free(nullptr); // no-op

	GTEXT_CSV_Reader * r = gtext_csv_reader_new(nullptr);
	ASSERT_NE(r, nullptr);
	EXPECT_EQ(gtext_csv_reader_next(r, nullptr), GTEXT_CSV_E_INVALID);
	EXPECT_EQ(gtext_csv_reader_feed(r, nullptr, 5, nullptr),
	    GTEXT_CSV_E_INVALID)
	    << "NULL with a non-zero length is not the end-of-input spelling";
	gtext_csv_reader_free(r);
}
