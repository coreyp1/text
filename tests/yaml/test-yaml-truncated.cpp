/**
 * @file test-yaml-truncated.cpp
 * @brief Truncated input must terminate, not spin.
 *
 * A block scalar header that runs to the end of input had no exit from the
 * loop that skips the rest of the header line: at EOF the peek returned -1,
 * the consume that followed could not advance the cursor, and the next pass
 * peeked -1 again. Two bytes - ">[" - hung the parser indefinitely, which is
 * a denial of service for anything parsing YAML it did not write.
 *
 * Found by the YAML fuzzer (tests/fuzz/fuzz_yaml.cpp).
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>

#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/** Parse and release; the point is that it returns at all. */
void parse_and_free(const char * data, size_t len) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(data, len, NULL, &err);
	if (doc) {
		gtext_yaml_free(doc);
	}
	gtext_yaml_error_free(&err);
}

} // namespace

TEST(YamlTruncated, BlockScalarHeaderAtEndOfInput) {
	// Each of these ends inside a block scalar header, the shortest being the
	// two bytes that first exposed the loop.
	const char *inputs[] = {
		">[",
		">",
		"|",
		">-",
		"|+",
		">2",
		"> #",
		"a: >",
		"a: |",
		"- >[",
		">\t",
	};

	for (const char *input : inputs) {
		parse_and_free(input, strlen(input));
	}
	SUCCEED() << "every truncated block scalar header returned";
}

TEST(YamlTruncated, EveryPrefixOfADocumentTerminates) {
	// A stronger version of the same property: no prefix of a valid document
	// may hang, whatever it cuts through.
	const char *doc =
		"key: >-\n"
		"  folded text\n"
		"other: |\n"
		"  literal text\n"
		"seq: [1, 2, 3]\n"
		"map: {a: 1}\n"
		"anchored: &a value\n"
		"alias: *a\n";

	for (size_t len = 0; len <= strlen(doc); len++) {
		parse_and_free(doc, len);
	}
	SUCCEED() << "every prefix returned";
}

TEST(YamlTruncated, BlockScalarBodyAtEndOfInput) {
	// A second loop with the same shape as the one above: the block scalar
	// body committed its consumption with `while (cursor < pos2)`, and
	// scanner_consume() cannot advance past the end of the input, so a pos2
	// beyond it spun forever. The fuzzer found this one only after the header
	// loop was fixed and it could reach deeper.
	//
	// The input is the fuzzer's own reproducer rather than a reconstruction:
	// a hand-written approximation of it did not trigger the loop, which is
	// exactly why the artifact is kept. Its first byte is the harness's parse
	// options selector and is skipped here.
	// Tests are run from the project root, as the other data-driven tests here
	// assume.
	const char *path = "tests/data/yaml/block-scalar-body-at-eof.bin";
	FILE *f = fopen(path, "rb");
	ASSERT_NE(f, nullptr) << "missing fixture: " << path;

	std::string data;
	char chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
		data.append(chunk, n);
	}
	fclose(f);
	ASSERT_GT(data.size(), 1u);

	const char *doc = data.data() + 1;
	size_t doc_len = data.size() - 1;

	parse_and_free(doc, doc_len);

	// And truncated at every length, since the failure depended on exactly
	// where the input ended.
	for (size_t len = 0; len <= doc_len; len++) {
		parse_and_free(doc, len);
	}
	SUCCEED() << "block scalar bodies ending at the input boundary returned";
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
