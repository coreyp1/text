/**
 * allow_complex_keys and require_string_keys, which nothing was asking about.
 *
 * Both are read in one place, validate_mapping_key(), and it runs between a
 * pair's key and its value rather than before or after both - the key has to
 * be resolved before its type can be judged, and the value has no business
 * being resolved if the key is going to be refused.
 *
 * That position is the whole reason this file exists. It was found by
 * mutation: deleting the validate_mapping_key() call outright, while the
 * resolver was being converted from a recursion to an explicit stack, broke
 * nothing in the suite. Grepping afterwards, neither option was set
 * deliberately by any test - one file set require_string_keys in passing,
 * to arrange something else. A check with no test is a check that can be
 * dropped by accident, and the conversion was exactly the kind of change
 * that drops one.
 */
#include <gtest/gtest.h>
#include <string.h>

#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

GTEXT_YAML_Status ParseWith(
		const std::string &src, bool complex_ok, bool strings_only) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.allow_complex_keys = complex_ok;
	opts.require_string_keys = strings_only;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(src.data(), src.size(), &opts, &err);
	GTEXT_YAML_Status st = doc ? GTEXT_YAML_OK : err.code;
	if (doc) gtext_yaml_free(doc);
	gtext_yaml_error_free(&err);
	return st;
}

}  // namespace

/* The control. Left on their defaults both options are off, so every document
   below is accepted - which is what makes the refusals further down
   attributable to the options rather than to the documents. */
TEST(YamlKeyValidation, TheDefaultsRefuseNoneOfThese) {
	GTEXT_YAML_Parse_Options d = gtext_yaml_parse_options_default();
	ASSERT_TRUE(d.allow_complex_keys);
	ASSERT_FALSE(d.require_string_keys);

	for (const char *src : {"{a: 1}", "{1: a}", "{[1, 2]: a}", "{{a: 1}: b}",
			"outer:\n  1: v\n", "outer:\n  [1]: v\n"}) {
		EXPECT_EQ(ParseWith(src, true, false), GTEXT_YAML_OK) << src;
	}
}

TEST(YamlKeyValidation, ComplexKeysCanBeRefused) {
	EXPECT_EQ(ParseWith("{a: 1}", false, false), GTEXT_YAML_OK);
	EXPECT_EQ(ParseWith("{1: a}", false, false), GTEXT_YAML_OK)
		<< "a scalar key is not a complex key";
	EXPECT_EQ(ParseWith("{[1, 2]: a}", false, false), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("{{a: 1}: b}", false, false), GTEXT_YAML_E_INVALID);
}

TEST(YamlKeyValidation, StringKeysCanBeRequired) {
	EXPECT_EQ(ParseWith("{a: 1}", true, true), GTEXT_YAML_OK);
	EXPECT_EQ(ParseWith("{\"1\": a}", true, true), GTEXT_YAML_OK)
		<< "quoted, so it is a string however it reads";
	EXPECT_EQ(ParseWith("{1: a}", true, true), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("{true: a}", true, true), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("{~: a}", true, true), GTEXT_YAML_E_INVALID);
}

/* Not only at the root. The resolver visits pairs one at a time and carries
   its position in the pair as state; a conversion that lost that state would
   plausibly still check the first key it met. */
TEST(YamlKeyValidation, ANestedKeyIsJudgedToo) {
	EXPECT_EQ(ParseWith("outer:\n  inner:\n    1: v\n", true, true),
		GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("outer:\n  inner:\n    [1]: v\n", false, false),
		GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("outer:\n  inner:\n    ok: v\n", true, true),
		GTEXT_YAML_OK);
}

/* Every key, not just the first, and not just the last. */
TEST(YamlKeyValidation, EachKeyInATurnIsJudged) {
	EXPECT_EQ(ParseWith("{a: 1, b: 2, c: 3}", true, true), GTEXT_YAML_OK);
	EXPECT_EQ(ParseWith("{1: x, b: 2, c: 3}", true, true), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("{a: 1, 2: x, c: 3}", true, true), GTEXT_YAML_E_INVALID);
	EXPECT_EQ(ParseWith("{a: 1, b: 2, 3: x}", true, true), GTEXT_YAML_E_INVALID);
}

/* The key is judged by what it resolves to, which is why the check cannot
   run before the key has been resolved. */
TEST(YamlKeyValidation, AnAliasIsJudgedByItsTarget) {
	const char *src = "defs: &n 1\nmap:\n  *n : v\n";
	EXPECT_EQ(ParseWith(src, true, true), GTEXT_YAML_E_INVALID)
		<< "the alias names an integer, so it is an integer key";

	const char *ok = "defs: &n text\nmap:\n  *n : v\n";
	EXPECT_EQ(ParseWith(ok, true, true), GTEXT_YAML_OK);
}
