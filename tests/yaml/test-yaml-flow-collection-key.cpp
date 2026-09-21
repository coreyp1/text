/**
 * A flow collection standing as a block mapping's key, anywhere but line one.
 *
 * ns-s-block-map-implicit-key is c-s-implicit-json-key or
 * ns-s-implicit-yaml-key (8.2.2), and the first of those is a
 * c-flow-json-node - so "[a]: b" and "{}: b" are block mappings whose key is
 * the flow collection. The parser knew that, and it worked:
 *
 *     {}: 1
 *
 * parsed. This did not:
 *
 *     a: 1
 *     {}: 2
 *
 * and neither did "[x]: 3" in its place, nor "{}: 2" under an indented
 * mapping. It was the *combination* that failed, which is what took so long
 * to see: three separate places asked where the key stands, and all three
 * asked it of the last scalar. A key that is a flow collection has no scalar
 * of its own - "{}" has none at all - so the answer they got was about
 * whatever came before, on whatever line that was. "{}: 1" alone parsed
 * because with nothing in front of it there was no earlier scalar to be
 * measured instead.
 *
 *   - the parser's block-entry guard measured last_scalar_offset, and
 *     refused the key for standing beside a node it was nowhere near;
 *   - the parser's key_indent came from the last scalar's line, and matched
 *     the enclosing mapping's indentation when it should not have, so the
 *     entry was refused for having no key at all;
 *   - the scanner's node_indent came from last_scalar_col, which set the
 *     fold boundary for the *value* too low: in "outer:" over "  {}: 1"
 *     over "  b: 2" the "b" folded into "1" and its own ":" was then
 *     orphaned.
 *
 * The first two are one line each in yaml_parser.c; the third is what
 * last_key_col exists for in scanner.c. Only the third changed anything
 * outside this shape: "[x]: 1" now measures from the "[" rather than from
 * the "x", so a continuation line one column further in folds where it
 * used to be refused - which is the last two rows here, and what js-yaml
 * does.
 *
 * Every row below was checked against js-yaml, which agrees on all of them,
 * refusals included. It was found by tests/fuzz/fuzz_yaml_writer.cpp, which
 * builds documents through the DOM API rather than parsing them, and so can
 * write a key a corpus of YAML text would never have carried in.
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
	/* On its own, which always worked, and is the control. */
	{"{}: 1\n", "{{}: 1}"},
	{"[]: 1\n", "{[]: 1}"},
	{"[x]: 3\n", "{[\"x\"]: 3}"},

	/* Not on the first line, which is the defect. */
	{"a: 1\n{}: 2\n", "{\"a\": 1, {}: 2}"},
	{"a: 1\n[x]: 3\n", "{\"a\": 1, [\"x\"]: 3}"},
	{"a: 1\n{k: v}: 2\n", "{\"a\": 1, {\"k\": \"v\"}: 2}"},
	{"a: 1\n{}: 2\nb: 3\n", "{\"a\": 1, {}: 2, \"b\": 3}"},
	{"{a: 1}: x\n{b: 2}: y\n", "{{\"a\": 1}: \"x\", {\"b\": 2}: \"y\"}"},
	{"[1, [2]]: x\na: 1\n", "{[1, [2]]: \"x\", \"a\": 1}"},

	/* A blank line and a comment line are not lines a key stands on. */
	{"a: 1\n\n{}: 2\n", "{\"a\": 1, {}: 2}"},
	{"a: 1\n# c\n{}: 2\n", "{\"a\": 1, {}: 2}"},

	/* Nested, where the key opens the mapping and where it does not. */
	{"outer:\n  {}: 1\n", "{\"outer\": {{}: 1}}"},
	{"outer:\n  a: 0\n  {}: 1\n", "{\"outer\": {\"a\": 0, {}: 1}}"},
	{"outer:\n  {}: 1\n  b: 2\n", "{\"outer\": {{}: 1, \"b\": 2}}"},
	{"outer:\n  []: 1\n  b: 2\n", "{\"outer\": {[]: 1, \"b\": 2}}"},
	{"outer:\n  [x]: 1\n  b: 2\n", "{\"outer\": {[\"x\"]: 1, \"b\": 2}}"},
	{"- a: 1\n  {}: 2\n", "[{\"a\": 1, {}: 2}]"},
	{"- {}: 1\n- {}: 2\n", "[{{}: 1}, {{}: 2}]"},

	/* What the guards are for, and still refuse: a second node beside one
	   already written on the line. */
	{"x: { y: z }in: valid\n", nullptr},
	{"a: 1 {}: 2\n", nullptr},
	{"a: {}: 2\n", nullptr},
	{"- { y: z }- invalid\n", nullptr},

	/* A bare scalar is no more a mapping entry after a flow key than after
	   a plain one - the point being that it is now refused for that reason
	   rather than folded into the value above it. */
	{"outer:\n  {}: 1\n  b\n", nullptr},
	{"outer:\n  a: 1\n  b\n", nullptr},

	/* The value's continuation line folds from where the *key* begins, so
	   one column further in than the "{" or "[" is inside the value. */
	{"outer:\n  {}: 1\n   c\n", "{\"outer\": {{}: \"1 c\"}}"},
	{"outer:\n  [x]: 1\n   c\n", "{\"outer\": {[\"x\"]: \"1 c\"}}"},

	/* A property in front of a key is part of the key: "&a {}" begins at the
	   "&", not at the "{". Both places that ask where a key stands were
	   taking the node's own column and ignoring what stood in front of it, so
	   with an anchor the mapping was indented to the "{" and the next entry
	   fell outside it - "&a {}: 1" over "b: 2" was refused as a second
	   top-level node, while "&a {}: 1" alone parsed. Without the property the
	   two columns are the same, which is what kept it hidden.

	   The scanner had the same blind spot, and there it was not new to flow
	   keys at all: "outer:" over "  &a x: 1" over "   c" was refused where
	   the same three lines without the "&a" fold into "1 c". */
	{"&a {}: 1\nb: 2\n", "{{}: 1, \"b\": 2}"},
	{"!!map {}: 1\nb: 2\n", "{{}: 1, \"b\": 2}"},
	{"b: 2\n&a {}: 1\n", "{\"b\": 2, {}: 1}"},
	{"&a {}: 1\n", "{{}: 1}"},
	{"outer:\n  &a {}: 1\n  b: 2\n", "{\"outer\": {{}: 1, \"b\": 2}}"},
	{"outer:\n  &a {}: 1\n   c\n", "{\"outer\": {{}: \"1 c\"}}"},
	{"outer:\n  &a x: 1\n   c\n", "{\"outer\": {\"x\": \"1 c\"}}"},
	{"outer:\n  x: 1\n   c\n", "{\"outer\": {\"x\": \"1 c\"}}"},

	/* And the same question a third time, where the entry above has no value
	   written. The rule that a flow collection at the key's own column is the
	   next entry's key was measuring the collection rather than the entry, so
	   with a property in front of it the columns no longer matched and the
	   collection landed where the value goes.

	   That rule applies only where the collection *begins its own line*.
	   "a: [b, c]" is a sequence on the same line as its key, and asking for
	   the line's first node there answers "a" - which would make the value
	   look like the next key. Ten documents of yaml-test-suite say so. */
	{"a:\n&k [x]:\n", "{\"a\": null, [\"x\"]: null}"},
	{"a:\n&k [x]: 1\n", "{\"a\": null, [\"x\"]: 1}"},
	{"a:\n[x]:\n", "{\"a\": null, [\"x\"]: null}"},
	{":\n&k {}:\n", "{null: null, {}: null}"},
	{"a: [b, c]\n", "{\"a\": [\"b\", \"c\"]}"},
	{"a: &k [b]\n", "{\"a\": [\"b\"]}"},

	/* And "begins its own line" has to apply the same rule the scanner does:
	   a "-", "?" or ":" is an indicator only where white space follows it.
	   The test skipped them without asking, so "-: {a: 1}" looked like a flow
	   mapping standing at the start of its line, the collection was taken for
	   the next entry's key, and the value it really was went missing. */
	{"-: {a: 1}\n", "{\"-\": {\"a\": 1}}"},
	{"-: !<t> {a: 1}\n", "{\"-\": {\"a\": 1}}"},
	{"-: [1]\nx: 2\n", "{\"-\": [1], \"x\": 2}"},
	{"?: {a: 1}\n", "{\"?\": {\"a\": 1}}"},
	{"- {}: 1\n", "[{{}: 1}]"},

	/* The entry above has no value written, so the flow key follows a key
	   that is still waiting for one.  These were not refused before - they
	   were accepted with the wrong shape, the second entry nested inside the
	   first as though the collection had been its value. */
	{"a:\n{}: 1\n", "{\"a\": null, {}: 1}"},
	{"[]:\n{}:\n", "{[]: null, {}: null}"},
	{"[]:\n{}: 2\n", "{[]: null, {}: 2}"},
	{"a: 1\n{}:\n", "{\"a\": 1, {}: null}"},
	{"[]:\n", "{[]: null}"},
	{"{}:\n", "{{}: null}"},

	/* Which is a rule about *flow* collections only. A block sequence may
	   stand at its own key's column and still be that key's value - that is
	   what "key:" over "- a" means (8.2.1) - and reading it as the next key
	   instead took eight documents of yaml-test-suite with it. A flow
	   collection has no such spelling: indented it is the value, at the
	   key's column it is the next key, and with no ":" after it at that
	   column it is an error. */
	{"key:\n- a\n- b\n", "{\"key\": [\"a\", \"b\"]}"},
	{"a:\n- x\nb: 1\n", "{\"a\": [\"x\"], \"b\": 1}"},
	{"key:\n {a: 1}\n", "{\"key\": {\"a\": 1}}"},
	{"key:\n {}: 1\n", "{\"key\": {{}: 1}}"},
	{"key:\n{a: 1}\n", nullptr},
	{"key:\n[1]\n", nullptr},
};

} // namespace

TEST(YamlFlowCollectionKey, StandsOnAnyLineOfABlockMapping) {
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

/* Two flow keys in one mapping are two keys, and the duplicate-key policy is
   what has an opinion about them being the same - not the grammar. */
TEST(YamlFlowCollectionKey, TwoEqualFlowKeysAreADuplicate) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	const char *input = "{}: 1\n{}: 2\n";
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_STREQ(err.message, "Duplicate mapping key");
	if (doc) gtext_yaml_free(doc);

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_KEEP_ALL;
	memset(&err, 0, sizeof(err));
	doc = gtext_yaml_parse(input, strlen(input), &opts, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "(no message)");
	std::string out;
	RenderInto(gtext_yaml_document_root(doc), out);
	EXPECT_EQ(out, "{{}: 1, {}: 2}");
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
