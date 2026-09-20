/**
 * Which tags a document may use, and which of those is a matter of policy.
 *
 * Two different questions were being answered by one switch. A tag in the
 * "tag:yaml.org,2002:" namespace names a type the spec defines, and one that
 * names something else - "!!bogus" - is a malformed document: the namespace
 * is not the author's to extend. A tag outside that namespace - "!foo", or a
 * global tag under somebody else's prefix - is exactly what tags are for, and
 * the spec's own examples are full of them; refusing those is a lockdown a
 * caller may want for untrusted input, not a correctness rule.
 *
 * allow_nonstandard_tags used to decide both. Left true, "!!bogus" parsed;
 * set false, thirteen of the specification's own examples stopped parsing -
 * 5TYM, 6CK3, 7FWL, C4HZ, UGM3 and the rest, every one of them a local or
 * global tag. Neither setting was right, because no single setting could be.
 *
 * So the namespace rule is now unconditional and the option governs only
 * application-defined tags, where it belongs.
 */
#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/* Parses, and says what tag the root ended up carrying. */
::testing::AssertionResult Accepts(
		const char *src, const GTEXT_YAML_Parse_Options *opts,
		const char *expect_tag) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), opts, &err);
	if (!doc) {
		return ::testing::AssertionFailure()
			<< "refused: " << (err.message ? err.message : "?");
	}
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const char *tag = root ? gtext_yaml_node_tag(root) : nullptr;
	::testing::AssertionResult result = ::testing::AssertionSuccess();
	if (expect_tag && (!tag || strcmp(tag, expect_tag) != 0)) {
		result = ::testing::AssertionFailure()
			<< "tag is " << (tag ? tag : "(none)")
			<< ", expected " << expect_tag;
	}
	gtext_yaml_free(doc);
	return result;
}

::testing::AssertionResult Refuses(
		const char *src, const GTEXT_YAML_Parse_Options *opts,
		const char *expect_message) {
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), opts, &err);
	if (doc) {
		gtext_yaml_free(doc);
		return ::testing::AssertionFailure() << "accepted";
	}
	if (err.code != GTEXT_YAML_E_INVALID) {
		return ::testing::AssertionFailure() << "code is " << (int)err.code;
	}
	if (!err.message || strcmp(err.message, expect_message) != 0) {
		return ::testing::AssertionFailure()
			<< "message is " << (err.message ? err.message : "(none)");
	}
	return ::testing::AssertionSuccess();
}

const char kUnknown[] = "Unknown tag in the tag:yaml.org,2002 namespace";
const char kPolicy[] = "Non-standard tag not allowed by parse options";

}  // namespace

/* The namespace rule: not the caller's to switch off. */

TEST(YamlTagPolicy, UnknownYamlNamespaceTagIsRefusedByDefault) {
	EXPECT_TRUE(Refuses("!!bogus 1\n", nullptr, kUnknown));
}

TEST(YamlTagPolicy, UnknownYamlNamespaceTagIsRefusedWrittenOutInFull) {
	EXPECT_TRUE(Refuses("!<tag:yaml.org,2002:bogus> 1\n", nullptr, kUnknown));
}

TEST(YamlTagPolicy, UnknownYamlNamespaceTagIsRefusedAnywhereInTheDocument) {
	EXPECT_TRUE(Refuses("a: !!bogus 1\n", nullptr, kUnknown));
	EXPECT_TRUE(Refuses("- !!bogus 1\n", nullptr, kUnknown));
	EXPECT_TRUE(Refuses("!!bogus 1: 2\n", nullptr, kUnknown));
	EXPECT_TRUE(Refuses("!!bogus [1]\n", nullptr, kUnknown));
	EXPECT_TRUE(Refuses("!!bogus {a: 1}\n", nullptr, kUnknown));
}

/* Turning off tag resolution, choosing another schema or asking for 1.1 all
   change how a tag is interpreted. None of them make an undefined one valid. */
TEST(YamlTagPolicy, NoOptionMakesAnUnknownYamlNamespaceTagValid) {
	GTEXT_YAML_Parse_Options opts;

	opts = gtext_yaml_parse_options_default();
	opts.resolve_tags = false;
	EXPECT_TRUE(Refuses("!!bogus 1\n", &opts, kUnknown));

	opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_FAILSAFE;
	EXPECT_TRUE(Refuses("!!bogus 1\n", &opts, kUnknown));

	opts = gtext_yaml_parse_options_default();
	opts.mode = GTEXT_YAML_MODE_CONFIG;
	EXPECT_TRUE(Refuses("!!bogus 1\n", &opts, kUnknown));

	opts = gtext_yaml_parse_options_default();
	opts.yaml_1_1 = true;
	EXPECT_TRUE(Refuses("!!bogus 1\n", &opts, kUnknown));

	opts = gtext_yaml_parse_options_default();
	opts.allow_nonstandard_tags = true;
	EXPECT_TRUE(Refuses("!!bogus 1\n", &opts, kUnknown));
}

/* "value" and "yaml" are named by the 1.1 type repository, but no schema here
   resolves them and both reference implementations refuse them. */
TEST(YamlTagPolicy, TypeRepositoryTagsThisLibraryDoesNotResolveAreRefused) {
	EXPECT_TRUE(Refuses("!!value 1\n", nullptr, kUnknown));
	EXPECT_TRUE(Refuses("!!yaml 1\n", nullptr, kUnknown));
}

TEST(YamlTagPolicy, TheDefinedYamlNamespaceTagsStillParse) {
	EXPECT_TRUE(Accepts("!!str 1\n", nullptr, "!!str"));
	EXPECT_TRUE(Accepts("!!int 1\n", nullptr, "!!int"));
	EXPECT_TRUE(Accepts("!!bool true\n", nullptr, "!!bool"));
	EXPECT_TRUE(Accepts("!!float 1.5\n", nullptr, "!!float"));
	EXPECT_TRUE(Accepts("!!null ~\n", nullptr, "!!null"));
	EXPECT_TRUE(Accepts("!!seq [1]\n", nullptr, "!!seq"));
	EXPECT_TRUE(Accepts("!!map {a: 1}\n", nullptr, "!!map"));
	EXPECT_TRUE(Accepts("!!set {a: ~}\n", nullptr, "!!set"));
	EXPECT_TRUE(Accepts("!!omap [{a: 1}]\n", nullptr, "!!omap"));
	EXPECT_TRUE(Accepts("!!pairs [{a: 1}]\n", nullptr, "!!pairs"));
	EXPECT_TRUE(Accepts("!!binary QQ==\n", nullptr, "!!binary"));
	EXPECT_TRUE(Accepts("!!timestamp 2025-02-14\n", nullptr, "!!timestamp"));
}

/* Application tags: the option's actual job. */

TEST(YamlTagPolicy, ApplicationTagsParseByDefault) {
	EXPECT_TRUE(Accepts("!foo 1\n", nullptr, "!foo"));
	EXPECT_TRUE(Accepts("!<tag:example.com,2000:app/x> 1\n", nullptr,
			"tag:example.com,2000:app/x"));
}

/* Spec examples 6.18, 6.19 and 6.22: a %TAG directive is expanded, so what
   reaches the policy is the global tag the handle stands for. */
TEST(YamlTagPolicy, DeclaredHandlesAreExpandedBeforeTheyArePoliced) {
	EXPECT_TRUE(Accepts("%TAG ! tag:example.com,2000:app/\n---\n!foo \"bar\"\n",
			nullptr, "tag:example.com,2000:app/foo"));
}

/* Example 6.19 redirects the secondary handle. "!!int" then has nothing to do
   with the YAML namespace, and the namespace rule must not claim it. */
TEST(YamlTagPolicy, ARedirectedSecondaryHandleLeavesTheYamlNamespace) {
	EXPECT_TRUE(Accepts("%TAG !! tag:example.com,2000:app/\n---\n!!int 1\n",
			nullptr, "tag:example.com,2000:app/int"));
}

/* ...and pointing a handle *into* the namespace does not smuggle a tag past
   the rule. */
TEST(YamlTagPolicy, AHandlePointedAtTheYamlNamespaceIsStillPoliced) {
	EXPECT_TRUE(Refuses("%TAG ! tag:yaml.org,2002:\n---\n!bogus 1\n",
			nullptr, kUnknown));
	EXPECT_TRUE(Accepts("%TAG ! tag:yaml.org,2002:\n---\n!str 1\n",
			nullptr, "tag:yaml.org,2002:str"));
}

TEST(YamlTagPolicy, ApplicationTagsAreRefusedWhenTheOptionIsOff) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.allow_nonstandard_tags = false;
	EXPECT_TRUE(Refuses("!foo 1\n", &opts, kPolicy));
	EXPECT_TRUE(Refuses("!<tag:example.com,2000:app/x> 1\n", &opts, kPolicy));
	/* The defined ones still parse: the option is about whose namespace the
	   tag is in, not about tags in general. */
	EXPECT_TRUE(Accepts("!!str 1\n", &opts, "!!str"));
}

TEST(YamlTagPolicy, SafeOptionsRefuseApplicationTags) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_safe();
	EXPECT_FALSE(opts.allow_nonstandard_tags);
	EXPECT_TRUE(Refuses("!foo 1\n", &opts, kPolicy));
}

/* A tag using a handle nobody declared is a third thing again, and keeps its
   own message. */
TEST(YamlTagPolicy, AnUndeclaredHandleIsReportedAsSuch) {
	EXPECT_TRUE(Refuses("!e!foo 1\n", nullptr,
			"Tag shorthand uses a handle no %TAG declared"));
}
