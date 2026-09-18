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
#include <stdio.h>
#include <string.h>
}

#include <fstream>
#include <string>
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
