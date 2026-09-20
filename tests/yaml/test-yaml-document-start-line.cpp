/**
 * What may be written on the "---" line.
 *
 * A block collection may not begin there. s-l+block-collection(n,c) reaches
 * l+block-mapping(n) or l+block-sequence(n) only through s-l-comments (8.2),
 * and once the line has content on it that needs a line break, so "--- a: b"
 * has no production. A scalar is another matter: "--- a", "--- !!str a" and
 * a block scalar header are ordinary bare documents, and a flow collection
 * is a flow node rather than a block one.
 *
 * "--- &anchor a: b" parsed as {"a": "b"} and "--- - a" as ["a"].
 *
 * yaml-test-suite has both mapping spellings as errors - CXX2
 * "--- &anchor a: b" and 9KBC "--- key1: value1" with a second key below it -
 * and PyYAML refuses every one of these. js-yaml is the outlier: it accepts
 * the plain "--- a: b" and "--- - a" while refusing the rest. The suite has
 * no case for a sequence either way; it is covered here because the same
 * production governs both, and treating them differently would be arbitrary.
 */
#include <gtest/gtest.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* Block collections on the "---" line. */
	{"--- &anchor a: b\n", nullptr},
	{"--- a: b\n", nullptr},
	{"--- !!map a: b\n", nullptr},
	{"--- key1: value1\n    key2: value2\n", nullptr},
	{"--- a: b\nc: d\n", nullptr},
	{"--- - a\n", nullptr},
	{"--- - a\n- b\n", nullptr},

	/* The same collections, one line further down. */
	{"---\na: b\n", "{\"a\": \"b\"}"},
	{"---\n- a\n", "[\"a\"]"},
	{"--- &anchor\na: b\n", "{\"a\": \"b\"}"},
	{"--- !!map\na: b\n", "{\"a\": \"b\"}"},

	/* Scalars and flow collections are not block collections. */
	{"--- a\n", "\"a\""},
	{"--- &a b\n", "\"b\""},
	{"--- !!str a\n", "\"a\""},
	{"--- [1, 2]\n", "[1, 2]"},
	{"--- {a: 1}\n", "{\"a\": 1}"},
	{"--- |\n  x\n", "\"x\\n\""},
	{"--- >\n  x\n", "\"x\\n\""},

	/* A "---" with nothing after it is the marker alone, not a prefix: a
	   mapping at column 0 on the next line is the document's root. */
	{"---\nkey1: value1\nkey2: value2\n",
	 "{\"key1\": \"value1\", \"key2\": \"value2\"}"},
};

} // namespace

TEST(YamlDocumentStartLine, BlockCollectionsMayNotBeginOnIt) {
	for (const Case &c : kCases) {
		const std::string got = Render(c.input);
		if (c.expected) {
			EXPECT_EQ(got, std::string(c.expected))
				<< "input: " << ::testing::PrintToString(std::string(c.input));
		} else {
			EXPECT_EQ(got, std::string(""))
				<< "should have been refused, input: "
				<< ::testing::PrintToString(std::string(c.input));
		}
	}
}

/* A "---" inside a scalar is content, not a marker. The check reads the line
   the node starts on, so a node whose line merely contains "---" somewhere
   else is unaffected. */
TEST(YamlDocumentStartLine, OnlyTheMarkerAtTheStartOfTheLineCounts) {
	EXPECT_EQ(Render("a: --- b\n"), std::string("{\"a\": \"--- b\"}"));
	EXPECT_EQ(Render("--- \"--- a\"\n"), std::string("\"--- a\""));
	EXPECT_EQ(Render("----: 1\n"), std::string("{\"----\": 1}"));
	EXPECT_EQ(Render("---x: 1\n"), std::string("{\"---x\": 1}"));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
