/**
 * @file
 *
 * A caller-supplied allocator must see every allocation the covered code
 * makes, and every matching free.  These tests use a counting allocator to
 * assert both: that the count is non-zero, so the allocator is really being
 * used, and that it returns to zero, so nothing leaked or was freed through
 * the C library instead.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <gtest/gtest.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/csv.h>
#include <ghoti.io/text/json.h>

namespace {

// A tracking allocator.  Each block is prefixed with its size so that
// outstanding bytes can be tracked, which is what catches a free() that went
// to the C library instead of here.
struct Counters {
	size_t live_blocks = 0;
	size_t total_allocations = 0;
	size_t live_bytes = 0;
	// Fail the allocation once this many have been served (0 = never).
	size_t fail_after = 0;
};

struct Header {
	size_t size;
	size_t guard;
};

const size_t kGuard = 0x5AFE5AFE5AFE5AFEull;

void * count_malloc(void * ctx, size_t size) {
	auto * c = static_cast<Counters *>(ctx);
	if (c->fail_after && c->total_allocations >= c->fail_after) {
		return nullptr;
	}
	void * raw = std::malloc(sizeof(Header) + (size ? size : 1));
	if (!raw) {
		return nullptr;
	}
	auto * h = static_cast<Header *>(raw);
	h->size = size;
	h->guard = kGuard;
	c->live_blocks++;
	c->total_allocations++;
	c->live_bytes += size;
	return static_cast<char *>(raw) + sizeof(Header);
}

void * count_calloc(void * ctx, size_t nitems, size_t size) {
	if (nitems && size > SIZE_MAX / nitems) {
		return nullptr; // overflow is an allocation failure, per the contract
	}
	size_t total = nitems * size;
	void * p = count_malloc(ctx, total ? total : 1);
	if (p) {
		std::memset(p, 0, total ? total : 1);
	}
	return p;
}

void count_free(void * ctx, void * ptr) {
	if (!ptr) {
		return;
	}
	auto * c = static_cast<Counters *>(ctx);
	auto * h = reinterpret_cast<Header *>(static_cast<char *>(ptr) - sizeof(Header));
	// If this fires, the pointer did not come from this allocator - which is
	// exactly the corruption a mismatched free() causes.
	ASSERT_EQ(h->guard, kGuard) << "freed a block this allocator never made";
	c->live_blocks--;
	c->live_bytes -= h->size;
	h->guard = 0;
	std::free(h);
}

void * count_realloc(void * ctx, void * ptr, size_t size) {
	if (!ptr) {
		return count_malloc(ctx, size);
	}
	auto * h = reinterpret_cast<Header *>(static_cast<char *>(ptr) - sizeof(Header));
	size_t old = h->size;
	void * fresh = count_malloc(ctx, size);
	if (!fresh) {
		return nullptr;
	}
	std::memcpy(fresh, ptr, old < size ? old : size);
	count_free(ctx, ptr);
	return fresh;
}

GTEXT_Allocator make_allocator(Counters * c) {
	GTEXT_Allocator a;
	a.ctx = c;
	a.malloc_fn = count_malloc;
	a.calloc_fn = count_calloc;
	a.realloc_fn = count_realloc;
	a.free_fn = count_free;
	return a;
}

} // namespace

TEST(Allocator, DefaultIsUsableAndHonorsTheZeroSizeContract) {
	const GTEXT_Allocator * d = gtext_allocator_default();
	ASSERT_NE(d, nullptr);

	// A zero-size request must return a usable pointer, so that NULL always
	// means failure.
	void * p = gtext_allocator_malloc(d, 0);
	EXPECT_NE(p, nullptr);
	gtext_allocator_free(d, p);

	void * z = gtext_allocator_calloc(d, 0, 0);
	EXPECT_NE(z, nullptr);
	gtext_allocator_free(d, z);

	// Overflow in calloc is an allocation failure, not a truncated block.
	EXPECT_EQ(gtext_allocator_calloc(d, SIZE_MAX, 2), nullptr);

	// A NULL allocator means the default everywhere.
	void * q = gtext_allocator_malloc(nullptr, 8);
	EXPECT_NE(q, nullptr);
	gtext_allocator_free(nullptr, q);

	// Freeing NULL is ignored.
	gtext_allocator_free(d, nullptr);
	gtext_allocator_free(nullptr, nullptr);
}

TEST(Allocator, JsonParseAndFreeBalanceThroughTheAllocator) {
	// Chosen to reach the paths that allocate outside the arena: object keys,
	// escaped strings needing a decode buffer, and preserved number lexemes.
	const char * src =
	    "{\"key\":\"plain\",\"esc\":\"a\\u00e9b\\n\",\"nested\":{\"deep\":[1,2,3]},"
	    "\"big\":123456789012345678901234567890,\"f\":1.5e10,\"t\":true,"
	    "\"n\":null,\"arr\":[\"x\",\"y\",{\"z\":[]}]}";

	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.allocator = &alloc;

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * v = gtext_json_parse(src, std::strlen(src), &opts, &err);
	ASSERT_NE(v, nullptr) << (err.message ? err.message : "parse failed");

	// The allocator was actually used, rather than quietly bypassed.
	EXPECT_GT(c.total_allocations, 0u);
	EXPECT_GT(c.live_blocks, 0u);

	gtext_json_free(v);
	gtext_json_error_free(&err);

	// Everything came back. A free() that went to the C library instead would
	// leave live_blocks above zero here, and would have tripped the guard.
	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, JsonParseBalancesOnTheErrorPath) {
	// Malformed input must not leak through the allocator either.
	const char * bad = "{\"a\":\"unterminated";
	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.allocator = &alloc;

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * v = gtext_json_parse(bad, std::strlen(bad), &opts, &err);
	EXPECT_EQ(v, nullptr);
	gtext_json_error_free(&err);

	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, JsonParseSurvivesAllocationFailure) {
	// An allocator that starts failing must produce a clean failure rather
	// than a crash or a leak.
	const char * src = "{\"a\":[1,2,3],\"b\":\"text\",\"c\":{\"d\":true}}";

	for (size_t budget = 1; budget <= 6; budget++) {
		Counters c;
		c.fail_after = budget;
		GTEXT_Allocator alloc = make_allocator(&c);
		GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
		opts.allocator = &alloc;

		GTEXT_JSON_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * v =
		    gtext_json_parse(src, std::strlen(src), &opts, &err);
		if (v) {
			gtext_json_free(v);
		}
		gtext_json_error_free(&err);
		EXPECT_EQ(c.live_blocks, 0u) << "leak with budget " << budget;
	}
}

TEST(Allocator, TwoParsesWithDifferentAllocatorsStaySeparate) {
	// The DOM records its allocator, so freeing one must not touch the other.
	const char * src = "{\"a\":[1,2,3]}";
	Counters c1, c2;
	GTEXT_Allocator a1 = make_allocator(&c1);
	GTEXT_Allocator a2 = make_allocator(&c2);

	GTEXT_JSON_Parse_Options o1 = gtext_json_parse_options_default();
	o1.allocator = &a1;
	GTEXT_JSON_Parse_Options o2 = gtext_json_parse_options_default();
	o2.allocator = &a2;

	GTEXT_JSON_Error e1, e2;
	std::memset(&e1, 0, sizeof(e1));
	std::memset(&e2, 0, sizeof(e2));
	GTEXT_JSON_Value * v1 = gtext_json_parse(src, std::strlen(src), &o1, &e1);
	GTEXT_JSON_Value * v2 = gtext_json_parse(src, std::strlen(src), &o2, &e2);
	ASSERT_NE(v1, nullptr);
	ASSERT_NE(v2, nullptr);

	size_t live2_before = c2.live_blocks;
	gtext_json_free(v1);
	EXPECT_EQ(c1.live_blocks, 0u);
	EXPECT_EQ(c2.live_blocks, live2_before) << "freeing one touched the other";

	gtext_json_free(v2);
	EXPECT_EQ(c2.live_blocks, 0u);
	gtext_json_error_free(&e1);
	gtext_json_error_free(&e2);
}

TEST(Allocator, NullAllocatorOptionStillParses) {
	// The default path must be unaffected by the new field.
	const char * src = "{\"a\":1}";
	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	EXPECT_EQ(opts.allocator, nullptr);

	GTEXT_JSON_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value * v = gtext_json_parse(src, std::strlen(src), &opts, &err);
	ASSERT_NE(v, nullptr);
	gtext_json_free(v);
	gtext_json_error_free(&err);
}

// ---------------------------------------------------------------------------
// The wrappers with no allocator
//
// Each of gtext_allocator_malloc/_calloc/_realloc/_free accepts NULL and falls
// back to gcu_allocator_default(), which is what lets every call site pass an
// optional allocator straight through without checking it first.  That
// fallback was reached by no test for _realloc, which tools/coverage.sh
// reported among the lines no test executes.
//
// The whole point of the NULL contract is that a caller need not care, so it
// has to work for all four, not three.
// ---------------------------------------------------------------------------

TEST(Allocator, NullMeansTheDefaultForEveryWrapper) {
	// malloc
	void * p = gtext_allocator_malloc(nullptr, 64);
	ASSERT_NE(p, nullptr);
	std::memset(p, 0xAB, 64);

	// realloc, growing - the contents must survive
	p = gtext_allocator_realloc(nullptr, p, 256);
	ASSERT_NE(p, nullptr);
	for (int i = 0; i < 64; ++i) {
		EXPECT_EQ(static_cast<unsigned char *>(p)[i], 0xAB) << "byte " << i;
	}

	// realloc, shrinking
	p = gtext_allocator_realloc(nullptr, p, 32);
	ASSERT_NE(p, nullptr);
	for (int i = 0; i < 32; ++i) {
		EXPECT_EQ(static_cast<unsigned char *>(p)[i], 0xAB) << "byte " << i;
	}

	gtext_allocator_free(nullptr, p);

	// realloc from NULL behaves as malloc
	void * q = gtext_allocator_realloc(nullptr, nullptr, 48);
	ASSERT_NE(q, nullptr);
	gtext_allocator_free(nullptr, q);

	// calloc zeroes
	unsigned char * z =
	    static_cast<unsigned char *>(gtext_allocator_calloc(nullptr, 16, 4));
	ASSERT_NE(z, nullptr);
	for (int i = 0; i < 64; ++i) {
		EXPECT_EQ(z[i], 0) << "byte " << i << " was not zeroed";
	}
	gtext_allocator_free(nullptr, z);

	// free(NULL, NULL) must be a no-op rather than a crash
	gtext_allocator_free(nullptr, nullptr);
}

TEST(Allocator, ExplicitDefaultMatchesTheNullFallback) {
	// Passing gtext_allocator_default() explicitly and passing NULL must be
	// the same thing, or the fallback is a second implementation.
	const GTEXT_Allocator * def = gtext_allocator_default();
	ASSERT_NE(def, nullptr);

	void * a = gtext_allocator_malloc(def, 32);
	void * b = gtext_allocator_malloc(nullptr, 32);
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);

	// Cross-free: a block from one must be releasable through the other.
	gtext_allocator_free(nullptr, a);
	gtext_allocator_free(def, b);

	void * c = gtext_allocator_malloc(def, 16);
	ASSERT_NE(c, nullptr);
	c = gtext_allocator_realloc(nullptr, c, 64);
	ASSERT_NE(c, nullptr);
	gtext_allocator_free(def, c);
}


// ---------------------------------------------------------------------------
// CSV
//
// The hazard documentation/formats/allocator-todo.md names is not a leak: a
// table whose arena came from the caller's allocator and whose structure came
// from the C library corrupts the heap when it is freed, and the caller cannot
// see it coming. The tracking allocator's guard word is what catches it - a
// free() that reached the C library instead leaves live_blocks above zero, and
// a free() of a block this allocator never made trips the guard outright.
//
// So every test below asserts both halves: that the count went up, so the
// allocator is really in the path, and that it came back to zero.
// ---------------------------------------------------------------------------

TEST(Allocator, CsvParseAndFreeBalanceThroughTheAllocator) {
	// Chosen to reach the paths that allocate: a header row and its map,
	// quoted fields needing an unescape buffer, an embedded newline, and
	// enough rows to grow past the initial capacity.
	std::string src = "name,value,note\n";
	for (int i = 0; i < 40; i++) {
		src += "row" + std::to_string(i) + ",\"a,b\",\"multi\nline\"\n";
	}

	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.allocator = &alloc;
	opts.dialect.treat_first_row_as_header = true;

	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table * t =
	    gtext_csv_parse_table(src.data(), src.size(), &opts, &err);
	ASSERT_NE(t, nullptr) << (err.message ? err.message : "parse failed");
	EXPECT_GT(c.total_allocations, 0u);
	EXPECT_GT(c.live_blocks, 0u);
	EXPECT_EQ(gtext_csv_row_count(t), 40u);

	gtext_csv_free_table(t);
	gtext_csv_error_free(&err);
	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, CsvTableMutationStaysWithTheTablesAllocator) {
	// The paths allocator-todo.md calls out by name: the temporary arrays in
	// the column operations, and the clone and compact paths that build a
	// second set of structures before swapping them in. Each takes the
	// allocator of the table it works on, so none of them may move a caller's
	// data onto the C heap.
	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);

	const char * headers[3] = {"a", "b", "c"};
	GTEXT_CSV_Table * t = gtext_csv_new_table_with_headers_and_allocator(
	    headers, nullptr, 3, &alloc);
	ASSERT_NE(t, nullptr);
	EXPECT_GT(c.total_allocations, 0u);

	for (int i = 0; i < 20; i++) {
		std::string v = "v" + std::to_string(i);
		const char * row[3] = {v.c_str(), "second", "third"};
		ASSERT_EQ(gtext_csv_row_append(t, row, nullptr, 3, nullptr),
		    GTEXT_CSV_OK);
	}

	// One entry per row, the header row included: csv_get_rows_to_modify()
	// counts it, so 20 data rows plus the header is 21.
	const char * col[21];
	col[0] = "d";
	for (int i = 1; i < 21; i++) {
		col[i] = "added";
	}
	EXPECT_EQ(
	    gtext_csv_column_append_with_values(t, "d", 1, col, nullptr),
	    GTEXT_CSV_OK);
	EXPECT_EQ(gtext_csv_normalize_to_max(t), GTEXT_CSV_OK);
	EXPECT_EQ(gtext_csv_table_compact(t), GTEXT_CSV_OK);

	GTEXT_CSV_Table * clone = gtext_csv_clone(t);
	ASSERT_NE(clone, nullptr) << "a clone inherits the source's allocator";

	gtext_csv_free_table(clone);
	gtext_csv_free_table(t);
	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, CsvStreamBalancesThroughTheAllocator) {
	// The streaming parser's own structure and its field buffer, which grows
	// across chunk boundaries - fed one byte at a time so that it must.
	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.allocator = &alloc;

	GTEXT_CSV_Event_cb cb = [](const GTEXT_CSV_Event *, void *) {
		return GTEXT_CSV_OK;
	};
	GTEXT_CSV_Stream * st = gtext_csv_stream_new(&opts, cb, nullptr);
	ASSERT_NE(st, nullptr);
	EXPECT_GT(c.total_allocations, 0u);

	std::string src = "a,\"quoted,field\",c\n";
	for (int i = 0; i < 30; i++) {
		src += "\"a long quoted field that forces the buffer to grow\",b,c\n";
	}
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	for (size_t i = 0; i < src.size(); i++) {
		ASSERT_EQ(gtext_csv_stream_feed(st, src.data() + i, 1, &err),
		    GTEXT_CSV_OK)
		    << (err.message ? err.message : "feed failed") << " at " << i;
	}
	EXPECT_EQ(gtext_csv_stream_finish(st, &err), GTEXT_CSV_OK);
	gtext_csv_stream_free(st);
	gtext_csv_error_free(&err);

	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

/*
 * Empty input takes its own path - csv_create_empty_table() rather than the
 * parse - and that path was not covered until a planted defect went unnoticed
 * by every test above. Planting the exact fault allocator-todo.md describes
 * (the table structure on the C library, its arena on the caller's allocator)
 * left the suite green, because nothing here had ever parsed zero bytes.
 */
TEST(Allocator, CsvEmptyInputBalancesThroughTheAllocator) {
	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.allocator = &alloc;

	GTEXT_CSV_Table * t = gtext_csv_parse_table("", 0, &opts, nullptr);
	ASSERT_NE(t, nullptr);
	EXPECT_GT(c.total_allocations, 0u) << "the allocator was bypassed";
	EXPECT_GT(c.live_blocks, 0u);
	EXPECT_EQ(gtext_csv_row_count(t), 0u);

	gtext_csv_free_table(t);
	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, CsvParseBalancesOnTheErrorPath) {
	Counters c;
	GTEXT_Allocator alloc = make_allocator(&c);
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.allocator = &alloc;

	const char * bad = "a,\"unterminated";
	GTEXT_CSV_Error err;
	std::memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table * t =
	    gtext_csv_parse_table(bad, std::strlen(bad), &opts, &err);
	if (t) {
		gtext_csv_free_table(t);
	}
	gtext_csv_error_free(&err);
	EXPECT_EQ(c.live_blocks, 0u);
	EXPECT_EQ(c.live_bytes, 0u);
}

TEST(Allocator, CsvParseSurvivesAllocationFailure) {
	const char * src = "a,b,c\n1,2,3\n4,\"5,5\",6\n";
	for (size_t budget = 1; budget <= 8; budget++) {
		Counters c;
		c.fail_after = budget;
		GTEXT_Allocator alloc = make_allocator(&c);
		GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
		opts.allocator = &alloc;

		GTEXT_CSV_Error err;
		std::memset(&err, 0, sizeof(err));
		GTEXT_CSV_Table * t =
		    gtext_csv_parse_table(src, std::strlen(src), &opts, &err);
		if (t) {
			gtext_csv_free_table(t);
		}
		gtext_csv_error_free(&err);
		EXPECT_EQ(c.live_blocks, 0u) << "leak with budget " << budget;
	}
}

TEST(Allocator, TwoCsvTablesWithDifferentAllocatorsStaySeparate) {
	// The table records its allocator, so freeing one must not touch the
	// other - and must not free through the other, which is what the guard
	// word would catch.
	const char * src = "a,b\n1,2\n";
	Counters c1, c2;
	GTEXT_Allocator a1 = make_allocator(&c1);
	GTEXT_Allocator a2 = make_allocator(&c2);

	GTEXT_CSV_Parse_Options o1 = gtext_csv_parse_options_default();
	o1.allocator = &a1;
	GTEXT_CSV_Parse_Options o2 = gtext_csv_parse_options_default();
	o2.allocator = &a2;

	GTEXT_CSV_Table * t1 =
	    gtext_csv_parse_table(src, std::strlen(src), &o1, nullptr);
	GTEXT_CSV_Table * t2 =
	    gtext_csv_parse_table(src, std::strlen(src), &o2, nullptr);
	ASSERT_NE(t1, nullptr);
	ASSERT_NE(t2, nullptr);

	size_t before = c2.live_blocks;
	gtext_csv_free_table(t1);
	EXPECT_EQ(c1.live_blocks, 0u);
	EXPECT_EQ(c2.live_blocks, before) << "freeing one touched the other";

	gtext_csv_free_table(t2);
	EXPECT_EQ(c2.live_blocks, 0u);
}

TEST(Allocator, CsvNullAllocatorOptionStillWorks) {
	// The default path must be unchanged: no allocator named, nothing
	// reaching a caller's allocator, and the table still correct.
	const char * src = "a,b\n1,2\n";
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	EXPECT_EQ(opts.allocator, nullptr) << "no allocator by default";

	GTEXT_CSV_Table * t =
	    gtext_csv_parse_table(src, std::strlen(src), &opts, nullptr);
	ASSERT_NE(t, nullptr);
	EXPECT_EQ(gtext_csv_row_count(t), 2u);
	gtext_csv_free_table(t);

	GTEXT_CSV_Table * plain = gtext_csv_new_table_with_allocator(nullptr);
	ASSERT_NE(plain, nullptr);
	gtext_csv_free_table(plain);
}
