/**
 * A tag or anchor standing where a key belongs.
 *
 * c-ns-properties may stand on their own, and the node they name is then the
 * empty node (7.2). So "!!str : 1" is the mapping {"": 1} - a key that was
 * never written, tagged as a string - and "&a : 1" is a null key carrying an
 * anchor. Both were refused outright, with the parser complaining that the
 * ":" had no key in front of it: the properties were being carried past the
 * colon and handed to the value instead, on the rule that a property shares
 * its node's line.
 *
 * The reason that rule cannot simply be dropped is the case it exists for.
 * A property at the *end* of a line introduces whatever the next line holds:
 *
 *     top3: &node3
 *       *alias1 : scalar3
 *
 * anchors the nested mapping, not an empty key (suite case 26DV, spec
 * example 2.27 in the same shape). The two are told apart by where the ":"
 * is. On the property's own line, with nothing between them, no key was
 * written and the properties belong to the empty one. On a later line, the
 * indentation rule decides, and this never fires.
 *
 * Both references agree with every answer here; PyYAML on all of them, and
 * js-yaml wherever it can tell a document-level property from a mapping.
 */
#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <ghoti.io/text/yaml.h>

#include "yaml_render.h"

namespace {

struct Case {
	const char *input;
	const char *expected; /* nullptr: must be refused */
};

const Case kCases[] = {
	/* The shapes that were refused. */
	{"!!str : 1\n", "{\"\": 1}"},
	{"!!str :\n", "{\"\": null}"},
	{"&anc : 1\n", "{null: 1}"},
	{"&a !!str : 1\n", "{\"\": 1}"},
	{"!!str  :  1\n", "{\"\": 1}"},

	/* Not only as the first key, and not only at the top level. */
	{"a: 1\n!!str : 2\n", "{\"a\": 1, \"\": 2}"},
	{"a: 1\n&anc : 2\n", "{\"a\": 1, null: 2}"},
	{"a:\n  !!str : 1\n", "{\"a\": {\"\": 1}}"},
	{"- !!str : 1\n", "[{\"\": 1}]"},

	/* The anchor names the empty key, so an alias to it is null. */
	{"&anc : 1\nb: *anc\n", "{null: 1, \"b\": null}"},

	/* The flow spelling, which worked already and must keep working
	 * (spec example 7.2, suite case WZ62). */
	{"{!!str : bar}\n", "{\"\": \"bar\"}"},

	/* A property at the end of a line introduces the next line's node.  This
	 * is the case the same-line rule exists for, and the one a naive fix
	 * breaks: each of these would gain an empty key instead. */
	{"top3: &node3\n  key : v\n", "{\"top3\": {\"key\": \"v\"}}"},
	{"a: !!str\nb: 1\n", "{\"a\": \"\", \"b\": 1}"},
	{"sequence: !!seq\n- entry\n", "{\"sequence\": [\"entry\"]}"},
	{"- &a\n- b\n", "[null, \"b\"]"},

	/* An explicit key spells the same thing out and already worked. */
	{"? !!str\n: 1\n", "{\"\": 1}"},

	/* The tag still has to fit the node it names.  An empty node is not an
	 * integer. */
	{"!!int : 1\n", nullptr},

	/* A property already claimed by a node on the line cannot also stand for
	 * an empty key. */
	{"a: !!str : 1\n", nullptr},
	{"? !!str : 1\n", nullptr},

	/* A property on a line of its own introduces what follows it, so there
	 * is no key here for the ":" on the next line. */
	{"!!str\n: 1\n", nullptr},
	{"&a\n: 1\n", nullptr},

	/* Only a ":" can leave properties behind this way.  A "]" or a "}" that
	 * closes nothing is not a node for them to name, and the document is
	 * malformed whatever they are attached to. */
	{"k: !!str ]\n", nullptr},
	{"k: !!str }\n", nullptr},
	{"k: &a ]\n", nullptr},
};

} // namespace

TEST(YamlPropertyEmptyKey, PropertiesStandingWhereAKeyBelongs) {
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

/* The rule is about the property's own line, and what it reports when it
 * does not apply is the other half of being right about that.
 *
 * "!!str" on one line and ":" on the next has no key anywhere - not one on
 * the wrong line, which is what the parser said when the line was not
 * checked, and which would send a reader looking for something that was
 * never written. */
TEST(YamlPropertyEmptyKey, AnOwnLinePropertyLeavesNoKeyBehind) {
	const char *inputs[] = {"!!str\n: 1\n", "&a\n: 1\n", "!custom\n: 1\n"};
	for (const char *input : inputs) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(input, strlen(input), nullptr, &err);
		ASSERT_EQ(doc, nullptr)
			<< "input: " << ::testing::PrintToString(std::string(input));
		ASSERT_NE(err.message, nullptr);
		EXPECT_STREQ(err.message, "Mapping key missing before ':'")
			<< "input: " << ::testing::PrintToString(std::string(input));
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
