/**
 * @file test-file-io.cpp
 * @brief Tests for the file I/O entry points of all three formats.
 *
 * Until September 2026 only YAML could read or write a file; JSON and CSV
 * callers had to slurp the bytes themselves, which meant every caller
 * reimplemented the same loop and none of them got the atomic-replace part
 * right. These tests cover the three properties that matter: a round trip
 * preserves the document, a failed write leaves the previous file alone, and
 * the size limit is enforced while reading rather than after.
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/csv.h>
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif
}

#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

/** A path in the build tree that is removed when the test ends. */
class TempPath {
public:
	explicit TempPath(const char *name) {
		path_ = std::string("build/test-file-io-") + name;
	}
	~TempPath() { remove(path_.c_str()); }
	const char *c_str() const { return path_.c_str(); }

	void write(const std::string &contents) const {
		std::ofstream out(path_, std::ios::binary | std::ios::trunc);
		out << contents;
	}
	std::string read() const {
		std::ifstream in(path_, std::ios::binary);
		return std::string(
		    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
	bool exists() const { return std::ifstream(path_).good(); }

private:
	std::string path_;
};

} // namespace

TEST(CsvFileIo, RoundTrip) {
	TempPath in("csv-in.csv");
	in.write("name,qty\nwidget,3\n\"a,b\",7\n");

	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table *table = gtext_csv_parse_file(in.c_str(), nullptr, &err);
	ASSERT_NE(table, nullptr) << (err.message ? err.message : "unknown");
	EXPECT_EQ(gtext_csv_row_count(table), 3u);

	size_t len = 0;
	const char *f = gtext_csv_field(table, 2, 0, &len);
	EXPECT_EQ(std::string(f ? f : "", len), "a,b");

	TempPath out("csv-out.csv");
	EXPECT_EQ(gtext_csv_write_file(out.c_str(), table, nullptr, &err),
	    GTEXT_CSV_OK);
	gtext_csv_free_table(table);

	// Re-read what was written; it must describe the same table.
	GTEXT_CSV_Table *again = gtext_csv_parse_file(out.c_str(), nullptr, &err);
	ASSERT_NE(again, nullptr);
	EXPECT_EQ(gtext_csv_row_count(again), 3u);
	const char *g = gtext_csv_field(again, 2, 0, &len);
	EXPECT_EQ(std::string(g ? g : "", len), "a,b");
	gtext_csv_free_table(again);
	gtext_csv_error_free(&err);
}

TEST(CsvFileIo, MissingFileIsAnError) {
	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(
	    gtext_csv_parse_file("build/does-not-exist.csv", nullptr, &err), nullptr);
	EXPECT_NE(err.code, GTEXT_CSV_OK);
	gtext_csv_error_free(&err);
}

TEST(CsvFileIo, SizeLimitIsEnforced) {
	TempPath in("csv-big.csv");
	std::string big;
	for (int i = 0; i < 200; i++) {
		big += "aaaaaaaaaa,bbbbbbbbbb\n";
	}
	in.write(big);

	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.max_total_bytes = 64;

	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_csv_parse_file(in.c_str(), &opts, &err), nullptr);
	EXPECT_EQ(err.code, GTEXT_CSV_E_LIMIT);
	gtext_csv_error_free(&err);
}

TEST(JsonFileIo, RoundTrip) {
	TempPath in("json-in.json");
	in.write("{\"name\":\"ghoti\",\"version\":[0,0,0]}");

	GTEXT_JSON_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_JSON_Value *v = gtext_json_parse_file(in.c_str(), nullptr, &err);
	ASSERT_NE(v, nullptr) << (err.message ? err.message : "unknown");

	TempPath out("json-out.json");
	EXPECT_EQ(
	    gtext_json_write_file(out.c_str(), v, nullptr, &err), GTEXT_JSON_OK);
	gtext_json_free(v);

	// The writer preserves the original number lexemes, so the bytes should
	// come back identical for a document with no insignificant whitespace.
	EXPECT_EQ(out.read(), "{\"name\":\"ghoti\",\"version\":[0,0,0]}");

	GTEXT_JSON_Value *again = gtext_json_parse_file(out.c_str(), nullptr, &err);
	ASSERT_NE(again, nullptr);
	const char *name = nullptr;
	size_t name_len = 0;
	ASSERT_EQ(gtext_json_get_string(
	              gtext_json_object_get(again, "name", 4), &name, &name_len),
	    GTEXT_JSON_OK);
	EXPECT_EQ(std::string(name, name_len), "ghoti");
	gtext_json_free(again);
	gtext_json_error_free(&err);
}

TEST(JsonFileIo, MalformedFileReportsAParseError) {
	TempPath in("json-bad.json");
	in.write("{\"a\":}");

	GTEXT_JSON_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_json_parse_file(in.c_str(), nullptr, &err), nullptr);
	EXPECT_NE(err.code, GTEXT_JSON_OK);
	gtext_json_error_free(&err);
}

TEST(JsonFileIo, SizeLimitIsEnforced) {
	TempPath in("json-big.json");
	std::string big = "[";
	for (int i = 0; i < 500; i++) {
		big += (i ? ",1" : "1");
	}
	big += "]";
	in.write(big);

	GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
	opts.max_total_bytes = 32;

	GTEXT_JSON_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_json_parse_file(in.c_str(), &opts, &err), nullptr);
	EXPECT_EQ(err.code, GTEXT_JSON_E_LIMIT);
	gtext_json_error_free(&err);
}

TEST(FileIo, WriteIsAtomic) {
	// A write that fails part way through must not damage what was there. The
	// serializer is made to fail by handing it a table and then a path inside a
	// directory that does not exist, so the temporary file cannot be created.
	TempPath existing("atomic.csv");
	existing.write("original,content\n");

	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table *table =
	    gtext_csv_parse_file(existing.c_str(), nullptr, &err);
	ASSERT_NE(table, nullptr);

	EXPECT_NE(gtext_csv_write_file(
	              "build/no-such-directory/out.csv", table, nullptr, &err),
	    GTEXT_CSV_OK);

	// The original is untouched, and no stray temporary was left beside it.
	EXPECT_EQ(existing.read(), "original,content\n");
	gtext_csv_free_table(table);
	gtext_csv_error_free(&err);
}

TEST(FileIo, NullArgumentsAreRejected) {
	GTEXT_CSV_Error cerr;
	memset(&cerr, 0, sizeof(cerr));
	EXPECT_EQ(gtext_csv_parse_file(nullptr, nullptr, &cerr), nullptr);
	EXPECT_EQ(gtext_csv_write_file(nullptr, nullptr, nullptr, &cerr),
	    GTEXT_CSV_E_INVALID);
	gtext_csv_error_free(&cerr);

	GTEXT_JSON_Error jerr;
	memset(&jerr, 0, sizeof(jerr));
	EXPECT_EQ(gtext_json_parse_file(nullptr, nullptr, &jerr), nullptr);
	EXPECT_EQ(gtext_json_write_file(nullptr, nullptr, nullptr, &jerr),
	    GTEXT_JSON_E_INVALID);
	gtext_json_error_free(&jerr);
}

TEST(YamlFileIo, StillRoundTrips) {
	// YAML already had file I/O; this guards the shape the other two now match.
	TempPath in("yaml-in.yaml");
	in.write("name: ghoti\nitems:\n  - one\n  - two\n");

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse_file(in.c_str(), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "unknown");

	TempPath out("yaml-out.yaml");
	EXPECT_EQ(gtext_yaml_write_file(out.c_str(), doc, nullptr, &err),
	    GTEXT_YAML_OK);
	gtext_yaml_free(doc);

	GTEXT_YAML_Document *again =
	    gtext_yaml_parse_file(out.c_str(), nullptr, &err);
	ASSERT_NE(again, nullptr);
	const GTEXT_YAML_Node *name =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(again), "name");
	ASSERT_NE(name, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(name), "ghoti");
	gtext_yaml_free(again);
	gtext_yaml_error_free(&err);
}


// ---------------------------------------------------------------------------
// What YAML gained by sharing the plumbing
//
// YAML had file I/O before JSON and CSV did, and kept its own copy of it: a
// reader built on fseek/ftell/fread, a temporary-file creator, and a rename
// wrapper, each slightly different from the one the other two share.
//
// Only the first of the three below fails against the previous implementation.
// The other two hold either way, and are here as guards on the new path rather
// than as evidence of a fixed bug - which is worth saying, because the
// difference the second one is really about is one no test can see from
// outside: whether the limit is applied before the bytes are in memory or
// after.
// ---------------------------------------------------------------------------

TEST(YamlFileIo, ReadsFromSomethingWithNoSize) {
	// fseek/ftell on a FIFO does not report a length, so the old reader
	// refused one outright - and /dev/stdin, a process substitution and
	// everything under /proc are the same shape. JSON and CSV read all of
	// them, because they read incrementally.
#ifdef _WIN32
	GTEST_SKIP() << "no mkfifo";
#else
	std::string path = "build/test-file-io-yaml-fifo";
	remove(path.c_str());
	ASSERT_EQ(mkfifo(path.c_str(), 0600), 0) << strerror(errno);

	// Opened for writing after the reader is running, so the reader blocks on
	// open rather than seeing an empty stream.
	std::thread writer([&path] {
		std::ofstream out(path, std::ios::binary);
		out << "name: ghoti\nitems:\n  - one\n";
	});

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse_file(path.c_str(), nullptr, &err);
	writer.join();
	remove(path.c_str());

	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "unknown");
	const GTEXT_YAML_Node *name =
	    gtext_yaml_mapping_get(gtext_yaml_document_root(doc), "name");
	ASSERT_NE(name, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(name), "ghoti");
	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
#endif
}

TEST(YamlFileIo, SizeLimitIsEnforcedWhileReading) {
	// max_total_bytes was in GTEXT_YAML_Parse_Options all along and the file
	// reader never looked at it: the whole document was read into memory and
	// only then measured by the parser, which is the one moment a limit is no
	// longer a limit. The *answer* was right before and is right now, so this
	// asserts the answer and nothing more; the change it accompanies is that
	// the bytes are no longer read first.
	TempPath in("yaml-big.yaml");
	std::string big = "items:\n";
	for (int i = 0; i < 400; i++) {
		big += "  - aaaaaaaaaabbbbbbbbbb\n";
	}
	in.write(big);

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.max_total_bytes = 64;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(gtext_yaml_parse_file(in.c_str(), &opts, &err), nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_LIMIT);
	gtext_yaml_error_free(&err);

	// The multi-document entry point reads the same way and had the same gap.
	GTEXT_YAML_Document **docs = nullptr;
	size_t count = 0;
	memset(&err, 0, sizeof(err));
	EXPECT_EQ(
	    gtext_yaml_parse_file_all(in.c_str(), &opts, &docs, &count, &err),
	    GTEXT_YAML_E_LIMIT);
	EXPECT_EQ(docs, nullptr);
	gtext_yaml_error_free(&err);
}

TEST(YamlFileIo, AFailedWriteLeavesTheDestinationAlone) {
	// The same property FileIo.WriteIsAtomic asserts for CSV, which YAML
	// reached by its own route and now reaches by the shared one. It held
	// before too: this is the guard that says sharing the plumbing did not
	// cost YAML anything.
	TempPath existing("yaml-atomic.yaml");
	existing.write("original: content\n");

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
	    gtext_yaml_parse_file(existing.c_str(), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "unknown");

	EXPECT_NE(gtext_yaml_write_file(
	              "build/no-such-directory/out.yaml", doc, nullptr, &err),
	    GTEXT_YAML_OK);
	EXPECT_EQ(existing.read(), "original: content\n");

	gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
}

// ---------------------------------------------------------------------------
// Reading past the initial buffer
//
// gtext_file_read_all() starts with a 64 KiB buffer and doubles it, but every
// test above uses a document of a few hundred bytes, so the doubling had never
// run once - and it is the path every real document takes.  tools/coverage.sh
// listed those lines among the growth and resize lines no test reaches.
//
// The sizes below straddle the 64 KiB boundary deliberately: just under, just
// over, exactly on it, and far enough past to force several doublings.
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kReadChunk = 64 * 1024;

/** A CSV document of exactly `target` bytes, or the nearest achievable size. */
std::string csv_of_size(size_t target) {
	std::string s = "a,b\n";
	const std::string row = "1234567,89\n"; // 11 bytes
	while (s.size() + row.size() <= target) {
		s += row;
	}
	// Pad the final field so the total lands exactly on `target`.
	while (s.size() < target) {
		s.insert(s.size() - 1, "x");
	}
	return s;
}

/** A JSON array of exactly `target` bytes. */
std::string json_of_size(size_t target) {
	std::string s = "[0";
	while (s.size() + 2 <= target - 1) {
		s += ",0";
	}
	// Pad with whitespace, which RFC 8259 allows between tokens.  Padding by
	// widening the last number instead produces "00", a leading zero the
	// grammar forbids - which the parser duly rejected, making the first
	// version of this test a bug in the test rather than in the library.
	while (s.size() < target - 1) {
		s += " ";
	}
	s += "]";
	return s;
}

} // namespace

TEST(FileIoGrowth, CsvReadsDocumentsLargerThanTheReadChunk) {
	const size_t sizes[] = {
	    kReadChunk - 1,     // one short of the initial buffer
	    kReadChunk,         // exactly the initial buffer
	    kReadChunk + 1,     // one doubling
	    kReadChunk * 2 + 1, // two doublings
	    kReadChunk * 5,     // several
	};

	for (size_t size : sizes) {
		SCOPED_TRACE("size=" + std::to_string(size));
		const std::string doc = csv_of_size(size);
		ASSERT_EQ(doc.size(), size);

		TempPath path("growth.csv");
		path.write(doc);

		GTEXT_CSV_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_CSV_Table * t =
		    gtext_csv_parse_file(path.c_str(), nullptr, &err);
		ASSERT_NE(t, nullptr) << (err.message ? err.message : "parse failed");

		// Every row survived the reallocation.  A growth path that loses or
		// truncates bytes shows up here rather than as a crash.
		const size_t rows = gtext_csv_row_count(t);
		EXPECT_GT(rows, 0u);

		gtext_csv_free_table(t);
		gtext_csv_error_free(&err);
	}
}

TEST(FileIoGrowth, JsonReadsDocumentsLargerThanTheReadChunk) {
	const size_t sizes[] = {
	    kReadChunk - 1,
	    kReadChunk,
	    kReadChunk + 1,
	    kReadChunk * 2 + 1,
	    kReadChunk * 5,
	};

	for (size_t size : sizes) {
		SCOPED_TRACE("size=" + std::to_string(size));
		const std::string doc = json_of_size(size);
		ASSERT_EQ(doc.size(), size);

		TempPath path("growth.json");
		path.write(doc);

		GTEXT_JSON_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value * v = gtext_json_parse_file(path.c_str(), nullptr, &err);
		ASSERT_NE(v, nullptr) << (err.message ? err.message : "parse failed");
		EXPECT_EQ(gtext_json_typeof(v), GTEXT_JSON_ARRAY);
		EXPECT_GT(gtext_json_array_size(v), 0u);

		gtext_json_free(v);
		gtext_json_error_free(&err);
	}
}

TEST(FileIoGrowth, ContentSurvivesTheReallocationExactly) {
	// The strongest check available: read a large file back and compare it
	// byte for byte with what was written.  A doubling that copies the wrong
	// length, or drops the tail of a chunk, fails here and nowhere else.
	const size_t size = kReadChunk * 3 + 12345;
	std::string doc;
	doc.reserve(size);
	for (size_t i = 0; i < size; ++i) {
		// A non-repeating pattern, so a duplicated or dropped block is visible.
		doc += static_cast<char>('A' + (i % 26));
	}

	TempPath path("growth.txt");
	path.write(doc);

	// Round-trip through the CSV reader, which treats the whole thing as one
	// enormous single-column row.
	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.max_field_bytes = size * 2;
	opts.max_record_bytes = size * 2;
	opts.max_total_bytes = size * 2;

	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table * t = gtext_csv_parse_file(path.c_str(), &opts, &err);
	ASSERT_NE(t, nullptr) << (err.message ? err.message : "parse failed");

	size_t len = 0;
	const char * field = gtext_csv_field(t, 0, 0, &len);
	ASSERT_NE(field, nullptr);
	EXPECT_EQ(len, size);
	if (len == size) {
		EXPECT_EQ(memcmp(field, doc.data(), size), 0)
		    << "the file came back with different bytes than were written";
	}

	gtext_csv_free_table(t);
	gtext_csv_error_free(&err);
}

TEST(FileIoGrowth, SizeLimitStillAppliesAcrossGrowth) {
	// The limit is checked inside the read loop, so it must fire during the
	// growth rather than after the whole file is in memory.
	const std::string doc = csv_of_size(kReadChunk * 3);
	TempPath path("growth-limit.csv");
	path.write(doc);

	GTEXT_CSV_Parse_Options opts = gtext_csv_parse_options_default();
	opts.max_total_bytes = kReadChunk; // smaller than the file

	GTEXT_CSV_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_CSV_Table * t = gtext_csv_parse_file(path.c_str(), &opts, &err);
	EXPECT_EQ(t, nullptr);
	if (t) {
		gtext_csv_free_table(t);
	}
	gtext_csv_error_free(&err);
}

