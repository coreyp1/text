/**
 * What a block scalar's indentation indicator counts from (8.1.1.1).
 *
 * It is the number of spaces the content is indented relative to the block
 * scalar's *parent node*, not relative to the line the header sits on. Those
 * are the same column often enough to hide the difference - "literal: |2" at
 * the root has both at 0 - and they part company as soon as the header is
 * not the first thing on its line:
 *
 *     - aaa: |2      the line begins at 0, but the mapping holding "aaa" is
 *         xxx        at 2, so the content is at 4 and "bbb" is a sibling
 *       bbb: |       key at 2.
 *         xxx
 *
 * Measuring from the line put the content at 2, so "bbb: |" and the rest of
 * the mapping were read as more lines of the first scalar and "aaa" came
 * back holding the whole document (suite cases 4WA9 and M5C3).
 *
 * Expectations here are shared by both references.
 */
#include <gtest/gtest.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected;
};

const Case kCases[] = {
	/* The header is not the first thing on its line, so the two bases
	   differ: content at 4, and the next key is a sibling. */
	{"- aaa: |2\n    xxx\n  bbb: |\n    xxx\n",
	 "[{\"aaa\": \"xxx\\n\", \"bbb\": \"xxx\\n\"}]"},
	{"- a: |1\n   x\n", "[{\"a\": \"x\\n\"}]"},

	/* At the root the parent is the line, which is the case that always
	   worked. */
	{"literal: |2\n  value\n", "{\"literal\": \"value\\n\"}"},
	{"a: |1\n  x\n", "{\"a\": \" x\\n\"}"},
	{"a: |2\n   x\n", "{\"a\": \" x\\n\"}"},

	/* A sequence entry's parent is the "-" itself. */
	{"- |2\n   x\n", "[\" x\\n\"]"},
	{"- - |2\n     x\n", "[[\" x\\n\"]]"},

	/* With no indicator the indentation is taken from the first content
	   line, which this rule does not touch. */
	{"a: |\n  x\n", "{\"a\": \"x\\n\"}"},
	{"- a: |\n    x\n", "[{\"a\": \"x\\n\"}]"},

	/* documentation/formats/yaml.md carried "block scalars nested inside a
	   mapping can swallow a sibling key" as a known limitation. It was this
	   same miscount, so these are here to keep it retired. */
	{"- a: |\n    x\n  b: c\n", "[{\"a\": \"x\\n\", \"b\": \"c\"}]"},
	{"- a: >\n    x\n  b: c\n", "[{\"a\": \"x\\n\", \"b\": \"c\"}]"},
	{"k:\n  a: |\n    x\n  b: c\n",
	 "{\"k\": {\"a\": \"x\\n\", \"b\": \"c\"}}"},
	{"k:\n  - a: |\n      x\n    b: c\n",
	 "{\"k\": [{\"a\": \"x\\n\", \"b\": \"c\"}]}"},
};

} // namespace

TEST(YamlBlockIndentIndicator, CountsFromTheParentNode) {
	for (const Case &c : kCases) {
		EXPECT_EQ(Render(c.input), std::string(c.expected))
			<< "input: " << ::testing::PrintToString(std::string(c.input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
