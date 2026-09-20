/**
 * Expanding a tag shorthand through a %TAG handle (6.8.2.2).
 *
 * A shorthand is a handle plus a suffix, and the handle says what the
 * suffix hangs off. Three handles exist: the primary "!", the secondary
 * "!!" - which means tag:yaml.org,2002: unless a directive says otherwise -
 * and any named "!handle!". All three may be redefined by %TAG.
 *
 * The secondary one could not be. Expansion returned early on any tag
 * beginning "!!", so
 *
 *     %TAG !! tag:example.com,2000:app/
 *     ---
 *     !!int 1 - 3
 *
 * still had "!!int" meaning tag:yaml.org,2002:int, and the document was
 * refused for holding an integer that is not one. It is spec example 6.19
 * (suite case P76L), and the value is the string "1 - 3": once the tag is
 * tag:example.com,2000:app/int, nothing in the core schema applies to it.
 *
 * Both references refuse every document here that defines a handle of its
 * own, because neither knows the tags it produces. That is a difference in
 * what the two do with an unknown tag rather than in how the shorthand
 * expands, and the suite sides with keeping the value.
 */
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* Spec example 6.19: the secondary handle redefined, so !!int is not
	   the core schema's int and "1 - 3" stays a string. */
	{"%TAG !! tag:example.com,2000:app/\n---\n!!int 1 - 3\n", "\"1 - 3\""},
	{"%TAG !! tag:example.com,2000:app/\n---\n!!int 5\n", "\"5\""},
	{"%TAG !! tag:example.com,2000:app/\n---\n!!str 5\n", "\"5\""},

	/* Without the directive the secondary handle is the standard one, and
	   redefining it *to* the standard prefix comes to the same thing. */
	{"!!int 5\n", "5"},
	{"!!str 5\n", "\"5\""},
	{"%TAG !! tag:yaml.org,2002:\n---\n!!int 5\n", "5"},

	/* A named handle expands the same way. */
	{"%TAG !e! tag:example.com,2000:app/\n---\n!e!foo bar\n", "\"bar\""},

	/* An unresolvable core-schema value under the standard handle is still
	   refused, which is what the first row would have done without the fix. */
	{"!!int 1 - 3\n", nullptr},

	/* A named handle exists only where a %TAG put it, and a shorthand using
	   one that was never declared is an error (6.8.2.2). It resolved to
	   itself and the document was accepted, which is how a handle declared
	   in the first document of a stream looked like it carried into the
	   rest: each document does get its own table, and the later ones simply
	   never complained (suite case QLJ7). */
	{"!prefix!A foo\n", nullptr},
	{"!e!\n", nullptr},
	{"%TAG !p! tag:example.com,2011:\n---\n!p!A foo\n", "\"foo\""},
	{"%TAG !p! tag:example.com,2011:\n---\n!p! foo\n", "\"foo\""},

	/* The primary and secondary handles need no directive, and a verbatim
	   tag has no handle at all. */
	{"!foo bar\n", "\"bar\""},
	{"! x\n", "\"x\""},
	{"!<tag:example.com,2000:x> y\n", "\"y\""},

	/* ns-uri-char allows "!", so a prefix may contain one and the expanded
	   tag then looks like a shorthand with a handle of its own. The check
	   only applies to tags still written as shorthands - expansion has
	   already happened by then - which is what the leading "!" test is for. */
	{"%TAG !p! tag:x!y!\n---\n!p!A foo\n", "\"foo\""},
	{"%TAG !p! tag:x!y!\n---\n!p! foo\n", "\"foo\""},
};

} // namespace

TEST(YamlTagHandles, TheSecondaryHandleMayBeRedefined) {
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

/* Handles may be prefixes of one another - "!" is a prefix of "!e!" - and
   the longest match is the one that applies, since that is the handle the
   shorthand was written with. The resolved tag is the only place this shows:
   both candidates expand to tags outside the core schema, so the value is
   the string "x" whichever is chosen. */
TEST(YamlTagHandles, TheLongestMatchingHandleWins) {
	const char *input =
		"%TAG ! tag:primary/\n"
		"%TAG !e! tag:named/\n"
		"---\n"
		"!e!foo x\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	/* "!e!" (three characters) beats "!" (one), so the suffix is "foo" and
	   not "e!foo". */
	EXPECT_STREQ(gtext_yaml_node_tag(root), "tag:named/foo");
	gtext_yaml_free(doc);
}

/* The primary handle is expanded too, and on its own it is the one that
   applies. */
TEST(YamlTagHandles, ThePrimaryHandleExpands) {
	const char *input =
		"%TAG ! tag:primary/\n"
		"---\n"
		"!foo x\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_STREQ(gtext_yaml_node_tag(root), "tag:primary/foo");
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
