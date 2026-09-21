/**
 * @file test-yaml-spec-corpus.cpp
 * @brief Score the parser against tests/data/yaml/spec-1.2.2.corpus.
 *
 * yaml-test-suite is a very good sample of YAML documents and it is still a
 * sample: 406 files against a grammar of some two hundred productions and a
 * resolution table. Passing all of it says what it says, and this project has
 * now been caught three times reading a score over a corpus as a statement
 * about the specification - 152 of 153 on a corpus grown from its own fixes,
 * "all 366 checked cases pass" over a denominator that dropped a tenth of the
 * suite, and 395 of 395 while eleven grammar rules went the wrong way.
 *
 * So the divergences found by reading the spec get a corpus of their own, and
 * it runs with the tests rather than behind `make conformance` - which needs
 * the network on first use and is not what anybody types before committing.
 *
 * The format is described at the top of the corpus file. It is deliberately
 * not YAML: a corpus read by the parser it is testing can be corrupted by the
 * very bug it is meant to catch.
 */

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/json.h>
#include <ghoti.io/text/yaml.h>
}

namespace {

struct Case {
	std::string name;
	std::string yaml;
	std::string want_json;   /* empty when the case expects an error */
	std::string want_error;  /* a substring of the message */
	int line = 0;            /* of the "@case" line, for failure output */
};

std::string corpus_path() {
	const char *dir = getenv("TEST_DATA_DIR");
	return std::string(dir ? dir : "tests/data/yaml") + "/spec-1.2.2.corpus";
}

void append_utf8(std::string *out, unsigned int cp) {
	if (cp < 0x80) {
		*out += (char)cp;
	} else if (cp < 0x800) {
		*out += (char)(0xC0 | (cp >> 6));
		*out += (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		*out += (char)(0xE0 | (cp >> 12));
		*out += (char)(0x80 | ((cp >> 6) & 0x3F));
		*out += (char)(0x80 | (cp & 0x3F));
	} else {
		*out += (char)(0xF0 | (cp >> 18));
		*out += (char)(0x80 | ((cp >> 12) & 0x3F));
		*out += (char)(0x80 | ((cp >> 6) & 0x3F));
		*out += (char)(0x80 | (cp & 0x3F));
	}
}

int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Escapes, in both the input and the expected output: "\t" a tab, "\0" a
 * NUL, "\xNN" and "\uNNNN" a code point, "\\" a backslash. Most of these
 * cases turn on a character that is invisible, or that an editor would eat,
 * or that cannot go in a source file at all - so none of them is written
 * literally. A backslash the expectation really contains is "\\": JSON
 * escapes are the one place that comes up, as in "\\t" for the two
 * characters the writer emits for a tab inside a string.
 *
 * An escape this does not know is left alone, backslash and all. */
std::string unescape(const std::string &in) {
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] != '\\' || i + 1 >= in.size()) { out += in[i]; continue; }
		switch (in[i + 1]) {
		case 't': out += '\t'; i++; break;
		case '0': out += '\0'; i++; break;
		case '\\': out += '\\'; i++; break;
		case 'x':
		case 'u': {
			const size_t digits = in[i + 1] == 'x' ? 2 : 4;
			if (i + 1 + digits >= in.size()) { out += in[i]; break; }
			unsigned int cp = 0;
			bool ok = true;
			for (size_t k = 0; k < digits; k++) {
				int v = hexval(in[i + 2 + k]);
				if (v < 0) { ok = false; break; }
				cp = (cp << 4) | (unsigned)v;
			}
			if (!ok) { out += in[i]; break; }
			append_utf8(&out, cp);
			i += 1 + digits;
			break;
		}
		default: out += in[i]; break;
		}
	}
	return out;
}

bool starts_with(const std::string &s, const char *prefix) {
	return s.compare(0, strlen(prefix), prefix) == 0;
}

/* Parses the corpus, or returns false with `why` set. A corpus that will not
 * parse has to fail the run rather than yield zero cases: a harness that
 * silently finds nothing is indistinguishable from a parser that gets
 * everything right. */
bool load_corpus(std::vector<Case> *out, std::string *why) {
	std::ifstream fh(corpus_path());
	if (!fh) { *why = "cannot open " + corpus_path(); return false; }

	enum { OUTSIDE, IN_YAML, IN_JSON } state = OUTSIDE;
	Case cur;
	std::string line;
	int lineno = 0;

	while (std::getline(fh, line)) {
		lineno++;
		if (!line.empty() && line.back() == '\r') line.pop_back();

		if (starts_with(line, "@case ")) {
			if (state != OUTSIDE) {
				*why = "@case inside a case at line " + std::to_string(lineno);
				return false;
			}
			cur = Case();
			cur.name = line.substr(6);
			cur.line = lineno;
			continue;
		}
		if (line == "@yaml") { state = IN_YAML; continue; }
		if (line == "@json") { state = IN_JSON; continue; }
		if (starts_with(line, "@error ")) {
			cur.want_error = line.substr(7);
			state = OUTSIDE;
			continue;
		}
		if (line == "@end") {
			if (cur.name.empty()) {
				*why = "@end with no @case at line " + std::to_string(lineno);
				return false;
			}
			if (cur.want_json.empty() && cur.want_error.empty()) {
				*why = "case '" + cur.name + "' expects nothing";
				return false;
			}
			out->push_back(cur);
			cur = Case();
			state = OUTSIDE;
			continue;
		}

		switch (state) {
		case IN_YAML: cur.yaml += unescape(line); cur.yaml += "\n"; break;
		case IN_JSON: cur.want_json += unescape(line); cur.want_json += "\n"; break;
		case OUTSIDE:
			if (!line.empty() && line[0] != '#') {
				*why = "stray text at line " + std::to_string(lineno)
					+ ": " + line;
				return false;
			}
			break;
		}
	}

	if (state != OUTSIDE || !cur.name.empty()) {
		*why = "corpus ends inside case '" + cur.name + "'";
		return false;
	}
	return true;
}

/* One JSON document per line, exactly as tools/conformance/yaml_test_suite.c
 * prints them, so a case can be moved between the two corpora unchanged. */
std::string render(const std::string &yaml, std::string *error_out) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	size_t count = 0;
	GTEXT_YAML_Document **docs =
		gtext_yaml_parse_all(yaml.c_str(), yaml.size(), &count, NULL, &err);
	if (!docs) {
		*error_out = err.message ? err.message : "(no message)";
		return std::string();
	}

	std::string out;
	for (size_t i = 0; i < count; i++) {
		GTEXT_JSON_Value *jv = NULL;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_To_JSON_Options opts = gtext_yaml_to_json_options_default();
		opts.allow_resolved_aliases = true;
		opts.allow_merge_keys = true;
		opts.coerce_keys_to_strings = true;
		if (gtext_yaml_to_json_with_options(docs[i], &jv, &opts, &err)
				!= GTEXT_YAML_OK) {
			*error_out = err.message ? err.message : "(no message)";
			for (size_t k = 0; k < count; k++) gtext_yaml_free(docs[k]);
			free(docs);
			return std::string();
		}
		if (!jv) { out += "null\n"; continue; }

		GTEXT_JSON_Sink sink;
		if (gtext_json_sink_buffer(&sink) == GTEXT_JSON_OK) {
			GTEXT_JSON_Error jerr;
			memset(&jerr, 0, sizeof(jerr));
			if (gtext_json_write_value(&sink, NULL, jv, &jerr) == GTEXT_JSON_OK) {
				out.append(gtext_json_sink_buffer_data(&sink),
					gtext_json_sink_buffer_size(&sink));
				out += "\n";
			} else {
				*error_out = jerr.message ? jerr.message : "(no message)";
				gtext_json_sink_buffer_free(&sink);
				gtext_json_free(jv);
				for (size_t k = 0; k < count; k++) gtext_yaml_free(docs[k]);
				free(docs);
				return std::string();
			}
			gtext_json_sink_buffer_free(&sink);
		}
		gtext_json_free(jv);
	}
	for (size_t i = 0; i < count; i++) gtext_yaml_free(docs[i]);
	free(docs);
	return out;
}

}  // namespace

TEST(YamlSpecCorpus, TheCorpusLoads) {
	std::vector<Case> cases;
	std::string why;
	ASSERT_TRUE(load_corpus(&cases, &why)) << why;
	/* A floor, not a count: adding a case must not mean editing this line,
	 * but an empty or truncated corpus has to be loud. */
	EXPECT_GE(cases.size(), 40u)
		<< "the corpus has shrunk; it held 45 cases when written";
}

TEST(YamlSpecCorpus, EveryCaseHoldsToTheSpecification) {
	std::vector<Case> cases;
	std::string why;
	ASSERT_TRUE(load_corpus(&cases, &why)) << why;

	for (const Case &c : cases) {
		SCOPED_TRACE("spec-1.2.2.corpus:" + std::to_string(c.line) + " "
			+ c.name);
		std::string error;
		std::string got = render(c.yaml, &error);

		if (!c.want_error.empty()) {
			EXPECT_TRUE(got.empty())
				<< "expected a refusal, got: " << got;
			EXPECT_NE(error.find(c.want_error), std::string::npos)
				<< "wanted a message containing \"" << c.want_error
				<< "\", got \"" << error << "\"";
			continue;
		}

		EXPECT_TRUE(error.empty()) << "refused a valid document: " << error;
		EXPECT_EQ(got, c.want_json);
	}
}
