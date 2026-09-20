/**
 * Numbers do not change meaning with the user's language settings.
 *
 * printf and strtod both read LC_NUMERIC, whose decimal separator is a comma
 * in a good many locales. Three conversions used the bare functions, and each
 * broke differently in such a locale:
 *
 *   - "a: 0.1" in YAML came back as the *string* "0.1". strtod stopped at the
 *     "." it did not recognise, the resolver read the partial parse as "not a
 *     number", and the scalar fell through to string.
 *   - gtext_json_new_number_double(0.1) built its lexeme with snprintf, so
 *     the number went into the document as "0,1" - which is not JSON.
 *   - A parsed JSON number quietly ended up with no double value, from the
 *     same partial parse leaving the has-double flag clear.
 *
 * The separator belongs to the format, not to whoever is running the program.
 *
 * These tests need a locale whose separator is a comma, and a machine may not
 * have one installed - the usual glibc build ships only C, POSIX and en_US.
 * Where none can be set they report that and pass, because there is nothing
 * to measure rather than nothing wrong; generate one with
 * "localedef -i de_DE -f UTF-8 <dir>/de_DE.UTF-8" and set LOCPATH to exercise
 * them for real.
 */
#include <gtest/gtest.h>
#include <locale.h>
#include <string.h>
#include <string>
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>

namespace {

/* Switches LC_NUMERIC to something that separates with a comma, and puts it
   back afterwards however the test ends. */
class CommaLocale {
public:
	CommaLocale() {
		const char *saved = setlocale(LC_NUMERIC, nullptr);
		if (saved) previous_ = saved;
		for (const char *name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8",
				"fr_FR.utf8", "es_ES.UTF-8", "nl_NL.UTF-8", "de_DE", "fr_FR"}) {
			if (setlocale(LC_NUMERIC, name)) {
				const lconv *lc = localeconv();
				if (lc && lc->decimal_point && strcmp(lc->decimal_point, ",") == 0) {
					active_ = true;
					name_ = name;
					return;
				}
			}
		}
		setlocale(LC_NUMERIC, previous_.c_str());
	}
	~CommaLocale() { setlocale(LC_NUMERIC, previous_.c_str()); }

	bool active() const { return active_; }
	const std::string &name() const { return name_; }

private:
	std::string previous_ = "C";
	std::string name_;
	bool active_ = false;
};

#define SKIP_WITHOUT_COMMA_LOCALE(loc)                                        \
	if (!(loc).active()) {                                                    \
		GTEST_SKIP() << "no comma-decimal locale installed; "                 \
			"generate one with localedef and set LOCPATH to run this";        \
		}

GTEXT_YAML_Node_Type YamlValueType(const char *src) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), nullptr, &err);
	if (!doc) return GTEXT_YAML_NULL;
	const GTEXT_YAML_Node *k = nullptr, *v = nullptr;
	gtext_yaml_mapping_get_at(gtext_yaml_document_root(doc), 0, &k, &v);
	GTEXT_YAML_Node_Type t = v ? gtext_yaml_node_type(v) : GTEXT_YAML_NULL;
	gtext_yaml_free(doc);
	return t;
}

std::string JsonFromDouble(double x) {
	GTEXT_JSON_Value *n = gtext_json_new_number_double(x);
	if (!n) return "";
	GTEXT_JSON_Sink sink;
	gtext_json_sink_buffer(&sink);
	GTEXT_JSON_Error err;
	memset(&err, 0, sizeof(err));
	gtext_json_write_value(&sink, nullptr, n, &err);
	std::string out(gtext_json_sink_buffer_data(&sink),
			gtext_json_sink_buffer_size(&sink));
	gtext_json_sink_buffer_free(&sink);
	gtext_json_free(n);
	return out;
}

}  // namespace

/* The locale helper itself: if this says a comma locale is active, the tests
   below are measuring something. */
TEST(LocaleNumbers, TheHarnessFindsACommaLocaleOrSaysWhyNot) {
	CommaLocale loc;
	if (loc.active()) {
		EXPECT_STREQ(localeconv()->decimal_point, ",") << loc.name();
	}
	else {
		GTEST_SKIP() << "no comma-decimal locale installed";
	}
}

TEST(LocaleNumbers, AYamlFloatIsStillAFloat) {
	EXPECT_EQ(YamlValueType("a: 0.1\n"), GTEXT_YAML_FLOAT) << "in the C locale";
	CommaLocale loc;
	SKIP_WITHOUT_COMMA_LOCALE(loc);
	EXPECT_EQ(YamlValueType("a: 0.1\n"), GTEXT_YAML_FLOAT) << loc.name();
	EXPECT_EQ(YamlValueType("a: -2.5\n"), GTEXT_YAML_FLOAT) << loc.name();
	EXPECT_EQ(YamlValueType("a: 1.5e3\n"), GTEXT_YAML_FLOAT) << loc.name();
	/* An integer was never at risk - no separator - but check it holds. */
	EXPECT_EQ(YamlValueType("a: 7\n"), GTEXT_YAML_INT) << loc.name();
}

TEST(LocaleNumbers, AJsonNumberBuiltFromADoubleIsStillJson) {
	EXPECT_EQ(JsonFromDouble(0.1), "0.10000000000000001") << "in the C locale";
	CommaLocale loc;
	SKIP_WITHOUT_COMMA_LOCALE(loc);
	const std::string out = JsonFromDouble(0.1);
	EXPECT_EQ(out, "0.10000000000000001") << loc.name();
	EXPECT_EQ(out.find(','), std::string::npos)
		<< "a comma in a JSON number: " << out;
}

TEST(LocaleNumbers, AParsedJsonNumberStillHasItsDouble) {
	const char *src = "{\"a\":0.1}";
	auto parse_double = [&](const char *label) {
		GTEXT_JSON_Parse_Options po = gtext_json_parse_options_default();
		po.parse_double = true;
		GTEXT_JSON_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_JSON_Value *v = gtext_json_parse(src, strlen(src), &po, &err);
		ASSERT_NE(v, nullptr) << label;
		double d = 0;
		EXPECT_EQ(gtext_json_get_double(gtext_json_object_get(v, "a", 1), &d),
				GTEXT_JSON_OK) << label;
		EXPECT_DOUBLE_EQ(d, 0.1) << label;
		gtext_json_free(v);
		gtext_json_error_free(&err);
	};
	parse_double("in the C locale");
	CommaLocale loc;
	SKIP_WITHOUT_COMMA_LOCALE(loc);
	parse_double(loc.name().c_str());
}

/* A YAML float key's JSON name carries a "." wherever it is converted. */
TEST(LocaleNumbers, ACoercedFloatKeyKeepsItsDecimalPoint) {
	auto convert = [](const char *label) {
		const char *src = "0.1: v\n";
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(src, strlen(src), nullptr, &err);
		ASSERT_NE(doc, nullptr) << label;
		GTEXT_JSON_Value *jv = nullptr;
		GTEXT_YAML_To_JSON_Options o = gtext_yaml_to_json_options_default();
		o.coerce_keys_to_strings = true;
		memset(&err, 0, sizeof(err));
		ASSERT_EQ(gtext_yaml_to_json_with_options(doc, &jv, &o, &err),
				GTEXT_YAML_OK) << label;
		GTEXT_JSON_Sink sink;
		gtext_json_sink_buffer(&sink);
		GTEXT_JSON_Error je;
		memset(&je, 0, sizeof(je));
		gtext_json_write_value(&sink, nullptr, jv, &je);
		const std::string out(gtext_json_sink_buffer_data(&sink),
				gtext_json_sink_buffer_size(&sink));
		gtext_json_sink_buffer_free(&sink);
		gtext_json_free(jv);
		gtext_yaml_free(doc);
		EXPECT_EQ(out, "{\"0.1\":\"v\"}") << label;
	};
	convert("in the C locale");
	CommaLocale loc;
	SKIP_WITHOUT_COMMA_LOCALE(loc);
	convert(loc.name().c_str());
}
