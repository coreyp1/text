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
#include <string.h>

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

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
