/**
 * Redefining an anchor.
 *
 * YAML 1.2 allows it: an alias "refers to the most recent preceding node
 * having the same anchor" (3.2.2.2), which only means anything if a name can
 * be reused. Spec example 7.1 is built on it - it has an "Override anchor"
 * line - and the parser refused the second definition outright, so that
 * example could not be parsed at all.
 *
 * This file used to assert the refusal. PyYAML agrees with the old
 * expectation and js-yaml with the new one; that is the usual 1.1-against-1.2
 * split, and 1.2 is what this parser targets.
 *
 * The binding has to be taken where the alias is written rather than from
 * the finished anchor map, or every alias would resolve to the last
 * definition of its name.
 */
#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

namespace {

/* The value at @p key, following it through if it is an alias: the DOM keeps
   the alias node and records what it resolved to rather than replacing it. */
const GTEXT_YAML_Node *value_of(const GTEXT_YAML_Document *doc, const char *key) {
	const GTEXT_YAML_Node *n =
		gtext_yaml_mapping_get(gtext_yaml_document_root(doc), key);
	if (n && gtext_yaml_node_type(n) == GTEXT_YAML_ALIAS) {
		return gtext_yaml_alias_target(n);
	}
	return n;
}

}  // namespace

TEST(YamlAnchors, AnAnchorMayBeRedefined) {
	const char *input = "a: &dup 1\n"
				"b: &dup 2\n";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), NULL, &err);

	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "a")), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "b")), "2");
	gtext_yaml_free(doc);
}

/* Each alias takes the definition that precedes it, not the last one in the
   document. */
TEST(YamlAnchors, AnAliasTakesTheMostRecentPrecedingDefinition) {
	const char *input = "a: &x 1\n"
				"b: *x\n"
				"c: &x 2\n"
				"d: *x\n";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), NULL, &err);

	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "b")), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "d")), "2");
	gtext_yaml_free(doc);
}

/* Spec example 7.1, which is where the rule comes from. */
TEST(YamlAnchors, SpecExample71) {
	const char *input =
		"First occurrence: &anchor Foo\n"
		"Second occurrence: *anchor\n"
		"Override anchor: &anchor Bar\n"
		"Reuse anchor: *anchor\n";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), NULL, &err);

	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "First occurrence")), "Foo");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "Second occurrence")), "Foo");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "Override anchor")), "Bar");
	EXPECT_STREQ(gtext_yaml_node_as_string(value_of(doc, "Reuse anchor")), "Bar");
	gtext_yaml_free(doc);
}

/* An alias to a name that was never defined is still an error. */
TEST(YamlAnchors, AnUnknownAnchorIsStillRefused) {
	const char *input = "a: *nope\n";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
