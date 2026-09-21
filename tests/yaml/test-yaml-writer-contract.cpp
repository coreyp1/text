/**
 * @file test-yaml-writer-contract.cpp
 * @brief What a writer must never do: claim success and produce something
 *        this library cannot read back.
 *
 * Running yaml-test-suite backwards measures the writers over documents that
 * came from parsing, and that is not the writers' domain.  The DOM API takes
 * any `char *` a caller hands it, so a value the parser would have refused is
 * ordinary on the way out - and a corpus of YAML text can never carry one,
 * because the parser refused it on the way in.  Every defect below was found
 * by building documents rather than by reading them.
 *
 *   - a character 5.1 forbids written raw, so the parser refused the writer's
 *     own output (34 code points);
 *   - an anchor name written without being checked, so "a b" wrote "&a b" and
 *     read back as the anchor "a" with the value shifted into it;
 *   - a tag percent-encoded on the way out but decoded on the way in only
 *     where a %TAG prefix applied, so "%" gained a layer of escaping on every
 *     round trip;
 *   - the streaming writer dropping the %YAML and %TAG directives, leaving a
 *     document whose tag handles were undefined.
 *
 * tests/fuzz/fuzz_yaml_writer.cpp searches the same space without a list.
 */

#include <gtest/gtest.h>
#include <string.h>

#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

struct Written {
	GTEXT_YAML_Status status;
	std::string text;
};

Written write_doc(const GTEXT_YAML_Document *doc, bool block = false) {
	Written w;
	GTEXT_YAML_Sink sink;
	EXPECT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
	if (block) {
		opts.pretty = true;
		opts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
	}
	w.status = gtext_yaml_write_document(doc, &sink, &opts);
	if (w.status == GTEXT_YAML_OK) {
		w.text.assign(gtext_yaml_sink_buffer_data(&sink),
			gtext_yaml_sink_buffer_size(&sink));
	}
	gtext_yaml_sink_buffer_free(&sink);
	return w;
}

/* The value that comes back, read with its length so an embedded NUL is
 * visible. */
bool read_back_scalar(const std::string &yaml, std::string *value) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(yaml.data(), yaml.size(), &opts, &err);
	gtext_yaml_error_free(&err);
	if (!doc) return false;
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const char *s = gtext_yaml_node_as_string(root);
	value->assign(s ? s : "", gtext_yaml_node_scalar_length(root));
	gtext_yaml_free(doc);
	return true;
}

std::string utf8(uint32_t cp) {
	std::string s;
	if (cp < 0x80) { s.push_back((char)cp); }
	else if (cp < 0x800) {
		s.push_back((char)(0xC0 | (cp >> 6)));
		s.push_back((char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s.push_back((char)(0xE0 | (cp >> 12)));
		s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		s.push_back((char)(0x80 | (cp & 0x3F)));
	} else {
		s.push_back((char)(0xF0 | (cp >> 18)));
		s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
		s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		s.push_back((char)(0x80 | (cp & 0x3F)));
	}
	return s;
}

}  // namespace

/* 5.1's c-printable is the set of characters a stream may hold.  Everything
   outside it needs an escape, and only the double-quoted style has escapes;
   the writer used to emit them raw and the parser - correctly - refused what
   it had just produced.  "\x92" was refused for a second reason: the scanner
   wrote the raw byte 0x92 rather than encoding U+0092, and a lone
   continuation byte is not UTF-8. */
TEST(YamlWriterContract, EveryCodePointSurvivesOrIsRefused) {
	/* The whole of C0 and C1, DEL, the two non-characters at the end of the
	   BMP, and enough ordinary neighbours to show the line is in the right
	   place. */
	for (uint32_t cp = 1; cp <= 0x10FFFF; cp++) {
		if (cp > 0xA5 && cp < 0xD7F0) cp = 0xD7F0;
		if (cp == 0xD800) cp = 0xE000;
		if (cp > 0xE005 && cp < 0xFFF0) cp = 0xFFF0;
		if (cp > 0x10005 && cp < 0x10FFFE) cp = 0x10FFFE;

		const std::string value = "a" + utf8(cp) + "b";
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		ASSERT_NE(doc, nullptr);
		GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar_n(
			doc, value.data(), value.size(), nullptr, nullptr);
		ASSERT_NE(root, nullptr);
		gtext_yaml_document_set_root(doc, root);

		Written w = write_doc(doc);
		gtext_yaml_free(doc);
		ASSERT_EQ(w.status, GTEXT_YAML_OK) << "U+" << std::hex << cp;

		std::string back;
		ASSERT_TRUE(read_back_scalar(w.text, &back))
			<< "U+" << std::hex << cp << " wrote " << w.text
			<< " which this parser refuses";
		EXPECT_EQ(back, value) << "U+" << std::hex << cp;
	}
}

/* The four that were written raw, kept by name so a regression says which
   rule broke rather than only that some code point did. */
TEST(YamlWriterContract, TheCharactersThatWereWrittenRaw) {
	struct Case { uint32_t cp; const char *spelling; };
	const Case cases[] = {
		{ 0x00, "\"a\\x00b\"" },   /* NUL: reachable only through the length API */
		{ 0x7F, "\"a\\x7Fb\"" },   /* DEL */
		{ 0x92, "\"a\\x92b\"" },   /* a C1 control */
		{ 0x85, "\"a\xc2\x85" "b\"" },  /* NEL *is* printable and stays raw */
		{ 0xFFFE, "\"a\\uFFFEb\"" },
	};
	for (const Case &c : cases) {
		const std::string value = "a" + utf8(c.cp) + "b";
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar_n(
			doc, value.data(), value.size(), nullptr, nullptr);
		gtext_yaml_document_set_root(doc, root);
		Written w = write_doc(doc);
		gtext_yaml_free(doc);
		ASSERT_EQ(w.status, GTEXT_YAML_OK);
		EXPECT_EQ(w.text, c.spelling) << "U+" << std::hex << c.cp;
		std::string back;
		ASSERT_TRUE(read_back_scalar(w.text, &back));
		EXPECT_EQ(back, value) << "U+" << std::hex << c.cp;
	}
}

/* Quoting a scalar changes its value, not only its spelling: only a plain
   scalar is resolved by its contents (10.3.2). So the writer may quote for
   style wherever it likes except where the plain text would have resolved to
   something other than a string - and it was doing exactly that for the two
   spellings whose characters its whitelist had left out. "~" is the null the
   10.3.2 table gives first; "+" leads the core schema's integer and float
   rows. Neither is a c-indicator and neither needed quoting at all. */
TEST(YamlWriterContract, AResolvableScalarIsNotQuotedIntoAString) {
	struct Case { const char *yaml; const char *json; };
	const Case cases[] = {
		{ "~",      "null" },
		{ "+1",     "1" },
		{ "+1.5",   "1.5" },
		{ "-1",     "-1" },
		{ ".inf",   "null" },   /* JSON has no infinity; both sides agree */
		{ "null",   "null" },
		{ "true",   "true" },
	};
	for (const Case &c : cases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.yaml, strlen(c.yaml), &popts, &err);
		ASSERT_NE(doc, nullptr) << c.yaml;
		gtext_yaml_error_free(&err);

		for (int block = 0; block < 2; block++) {
			Written w = write_doc(doc, block != 0);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << c.yaml;
			GTEXT_YAML_Document *back = gtext_yaml_parse(
				w.text.data(), w.text.size(), &popts, &err);
			ASSERT_NE(back, nullptr) << c.yaml << " wrote " << w.text;
			gtext_yaml_error_free(&err);
			const GTEXT_YAML_Node *root = gtext_yaml_document_root(back);
			/* The point is the node's *type*: a quoted "~" is a string. */
			EXPECT_EQ(gtext_yaml_node_type(root),
				gtext_yaml_node_type(gtext_yaml_document_root(doc)))
				<< c.yaml << " wrote " << w.text
				<< " which reads back as a different kind of scalar";
			gtext_yaml_free(back);
		}
		gtext_yaml_free(doc);
	}
}

/* gtext_yaml_node_new_scalar() used to make a *string* of whatever it was
   given, which is a default rather than an assertion, and it made both halves
   wrong: gtext_yaml_node_type() said "string" of a node holding "1", and the
   writer, told it was a string, wrote it plain - so it came back as the
   integer 1, and there was no way through the DOM API to write a string that
   looks like a number.

   The plain constructor takes the type from the text now, as a parse of the
   same characters would, and gtext_yaml_node_new_scalar_typed() is where a
   caller says otherwise. A string the text would not have produced goes out
   quoted, because only a plain scalar is resolved by its contents (10.3.2).
   The writer asks the resolver's own predicate, not a second copy of the
   10.3.2 tables. */
TEST(YamlWriterContract, AStringThatSpellsANumberIsQuoted) {
	const char *strings[] = {
		"1", "-1", "+1", "1.5", "0x1f", "0o17", ".inf", ".nan", "-.inf",
		"true", "False", "NULL", "~", "",
	};
	for (const char *value : strings) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar_typed(
			doc, value, strlen(value), GTEXT_YAML_STRING, nullptr, nullptr);
		ASSERT_NE(root, nullptr) << value;
		ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING) << value;
		gtext_yaml_document_set_root(doc, root);

		for (int block = 0; block < 2; block++) {
			Written w = write_doc(doc, block != 0);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << value;
			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(w.text.data(), w.text.size(), &opts, &err);
			ASSERT_NE(back, nullptr) << value << " wrote " << w.text;
			gtext_yaml_error_free(&err);
			const GTEXT_YAML_Node *r = gtext_yaml_document_root(back);
			EXPECT_EQ(gtext_yaml_node_type(r), GTEXT_YAML_STRING)
				<< "the string <<" << value << ">> wrote " << w.text
				<< " which reads back as something else";
			const char *got = gtext_yaml_node_as_string(r);
			EXPECT_STREQ(got ? got : "", value) << "wrote " << w.text;
			gtext_yaml_free(back);
		}
		gtext_yaml_free(doc);
	}
}

/* And the plain constructor agrees with a parse of the same characters. */
TEST(YamlWriterContract, ABuiltScalarHasTheTypeItsTextWouldParseAs) {
	struct Case { const char *text; GTEXT_YAML_Node_Type type; };
	const Case cases[] = {
		{ "1", GTEXT_YAML_INT },       { "-1", GTEXT_YAML_INT },
		{ "0x1f", GTEXT_YAML_INT },    { "1.5", GTEXT_YAML_FLOAT },
		{ ".inf", GTEXT_YAML_FLOAT },  { "true", GTEXT_YAML_BOOL },
		{ "False", GTEXT_YAML_BOOL },  { "null", GTEXT_YAML_NULL },
		{ "~", GTEXT_YAML_NULL },      { "", GTEXT_YAML_NULL },
		{ "x", GTEXT_YAML_STRING },    { "01", GTEXT_YAML_STRING },
		{ "0b101", GTEXT_YAML_STRING },/* 1.1, and this is 1.2 */
		{ "1_000", GTEXT_YAML_STRING },
	};
	for (const Case &c : cases) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *node =
			gtext_yaml_node_new_scalar(doc, c.text, nullptr, nullptr);
		ASSERT_NE(node, nullptr) << c.text;
		EXPECT_EQ(gtext_yaml_node_type(node), c.type) << c.text;

		/* And a parse of the same characters says the same thing. */
		if (*c.text) {
			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
			GTEXT_YAML_Document *parsed =
				gtext_yaml_parse(c.text, strlen(c.text), &opts, &err);
			ASSERT_NE(parsed, nullptr) << c.text;
			gtext_yaml_error_free(&err);
			EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(parsed)),
				c.type) << c.text;
			gtext_yaml_free(parsed);
		}
		gtext_yaml_free(doc);
	}
}

/* White space at either end of a built scalar's text makes it a string, and
   the reason is worth stating: a plain scalar's content has none - ns-plain
   begins and ends with an ns-char (7.3.3) - so no plain spelling of such text
   exists, only a quoted one, and quoted is string (10.3.2).

   strtoll() skips leading white space, which is how " 3", "\t3" and "\n3"
   came to answer "the integer 3" for a node the writer then quoted and the
   reader read back as a string. The trailing end was already right, which is
   why only half of this ever showed. It is not a question the corpus can put:
   a parse of " 3" is the integer 3, because the scanner takes the space off
   before any of this is asked - the text only reaches here through the DOM
   API, and the writer fuzzer is what put it there. */
TEST(YamlWriterContract, OuterWhiteSpaceMakesABuiltScalarAString) {
	const char *texts[] = {
		" 3", "\t3", "\n3", "3 ", "3\n", "\ntrue", " ~", " ", "\n",
		/* And the middle, which the ends-only test left. This code consumes
		   the sign itself and hands strtoll() what follows, and strtoll()
		   skips leading white space - so "+\n1" was the integer 1 and
		   "0x\n10" was 16. Not one row of the 10.3.2 table contains white
		   space anywhere, so the rule is simply that text carrying any is a
		   string, and the helper now wants a digit straight after the sign
		   on its own account as well. */
		"+\n1", "+ 1", "-\n1", "0x\n10", "1 1", "1\n1", "+\n\n1",
	};
	for (const char *text : texts) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *node =
			gtext_yaml_node_new_scalar(doc, text, nullptr, nullptr);
		ASSERT_NE(node, nullptr) << ::testing::PrintToString(std::string(text));
		EXPECT_EQ(gtext_yaml_node_type(node), GTEXT_YAML_STRING)
			<< ::testing::PrintToString(std::string(text));

		/* And what the writer produces reads back as the same string - which
		   is the property the fuzzer checks, stated for these nine. */
		gtext_yaml_document_set_root(doc, node);
		Written w = write_doc(doc);
		ASSERT_EQ(w.status, GTEXT_YAML_OK)
			<< ::testing::PrintToString(std::string(text));
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		GTEXT_YAML_Document *back =
			gtext_yaml_parse(w.text.c_str(), w.text.size(), &opts, &err);
		ASSERT_NE(back, nullptr) << "wrote " << w.text;
		gtext_yaml_error_free(&err);
		const GTEXT_YAML_Node *r = gtext_yaml_document_root(back);
		EXPECT_EQ(gtext_yaml_node_type(r), GTEXT_YAML_STRING) << "wrote " << w.text;
		const char *got = gtext_yaml_node_as_string(r);
		EXPECT_STREQ(got ? got : "", text) << "wrote " << w.text;
		gtext_yaml_free(back);
		gtext_yaml_free(doc);
	}
}

/* A line of exactly "---" or "..." is c-directives-end or c-document-end
   (9.1.2), and c-forbidden keeps either out of a document's content wherever
   it begins a line with a break, white space or end of input after it
   (9.1.1). The whitelist is about characters and every one of these is "-" or
   ".", all of which it admits, so the string "---" went out plain - and the
   writer had then produced a document marker and called it OK. The reader
   agreed with the bytes and handed back an empty document.

   Quoting is value-preserving here, since neither text resolves to anything
   but a string, so the whole family is quoted and not just the positions
   where it would be fatal. "----" and "---x" stay plain: c-forbidden wants a
   break or white space after the three characters, and both references read
   those two as the strings they are. */
TEST(YamlWriterContract, ADocumentMarkerIsNotWrittenAsAPlainScalar) {
	struct Case { const char *text; bool quoted; };
	const Case cases[] = {
		{ "---", true },   { "...", true },
		{ "--- x", true }, { "... x", true },
		{ "----", false }, { "---x", false },
		{ "...x", false }, { "..", false },
		{ "--", false },
	};
	for (const Case &c : cases) {
		for (int block = 0; block < 2; ++block) {
			GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
			GTEXT_YAML_Node *node = gtext_yaml_node_new_scalar_typed(
				doc, c.text, strlen(c.text), GTEXT_YAML_STRING,
				nullptr, nullptr);
			ASSERT_NE(node, nullptr) << c.text;
			gtext_yaml_document_set_root(doc, node);
			Written w = write_doc(doc, block != 0);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << c.text;
			EXPECT_EQ(w.text.find('"') != std::string::npos, c.quoted)
				<< "wrote " << w.text << " for <<" << c.text << ">>";

			/* Whatever it chose, the bytes have to read back as this string
			   and not as a marker - which is the property that failed. */
			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(w.text.c_str(), w.text.size(), &opts, &err);
			ASSERT_NE(back, nullptr) << "wrote " << w.text;
			gtext_yaml_error_free(&err);
			const GTEXT_YAML_Node *r = gtext_yaml_document_root(back);
			ASSERT_NE(r, nullptr) << "wrote " << w.text
				<< " which reads back as an empty document";
			const char *got = gtext_yaml_node_as_string(r);
			EXPECT_STREQ(got ? got : "", c.text) << "wrote " << w.text;
			gtext_yaml_free(back);
			gtext_yaml_free(doc);
		}
	}
}

/* But a scalar the parser resolved to a number stays plain: quoting it would
   make a string of it, which is the same fault in the other direction. */
TEST(YamlWriterContract, AResolvedScalarIsNotQuotedIntoAString) {
	const char *documents[] = { "1", "-1", "1.5", "true", "null", "~", ".inf" };
	for (const char *src : documents) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), &opts, &err);
		ASSERT_NE(doc, nullptr) << src;
		gtext_yaml_error_free(&err);
		const GTEXT_YAML_Node_Type want =
			gtext_yaml_node_type(gtext_yaml_document_root(doc));

		for (int block = 0; block < 2; block++) {
			Written w = write_doc(doc, block != 0);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << src;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(w.text.data(), w.text.size(), &opts, &err);
			ASSERT_NE(back, nullptr) << src << " wrote " << w.text;
			gtext_yaml_error_free(&err);
			EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(back)), want)
				<< src << " wrote " << w.text;
			gtext_yaml_free(back);
		}
		gtext_yaml_free(doc);
	}
}

/* A scalar has to be written in a style that reads back as a scalar.
   ns-plain-first admits "-" only when an ns-plain-safe character follows it;
   a lone "-" on a line is a block sequence entry, so the string "-" written
   plain came back as a sequence holding one empty node. */
TEST(YamlWriterContract, AScalarIsNeverWrittenAsSomethingElse) {
	const char *values[] = { "-", "- ", "-\tx", "- x" };
	for (const char *value : values) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root =
			gtext_yaml_node_new_scalar(doc, value, nullptr, nullptr);
		gtext_yaml_document_set_root(doc, root);
		for (int block = 0; block < 2; block++) {
			Written w = write_doc(doc, block != 0);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << value;
			std::string back;
			ASSERT_TRUE(read_back_scalar(w.text, &back))
				<< "<<" << value << ">> wrote " << w.text;
			EXPECT_EQ(back, value) << "wrote " << w.text;
		}
		gtext_yaml_free(doc);
	}

	/* "-1" and "-x" are ordinary plain scalars: something plain-safe follows
	   the "-", which is the whole of the rule. */
	const char *plain[] = { "-1", "-x", "-.inf" };
	for (const char *value : plain) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root =
			gtext_yaml_node_new_scalar(doc, value, nullptr, nullptr);
		gtext_yaml_document_set_root(doc, root);
		Written w = write_doc(doc);
		gtext_yaml_free(doc);
		ASSERT_EQ(w.status, GTEXT_YAML_OK) << value;
		EXPECT_EQ(w.text, std::string(value)) << "quoted unnecessarily";
	}
}

/* A YAML stream is a stream of *characters*, and every escape of 5.7 names a
   code point, so a byte that is not part of any UTF-8 sequence has no
   spelling at all. Writing "\\xFF" for the byte 0xFF would read back as
   U+00FF - two bytes, a different value - so it is refused rather than
   quietly changed into the character that happens to share its number. The
   writer fuzzer found this one. */
TEST(YamlWriterContract, AScalarThatIsNotUtf8IsRefused) {
	const std::string bad[] = {
		std::string("\xff", 1),
		std::string("a\xff" "b", 3),
		std::string("\xc2", 1),          /* a lead byte with no continuation */
		std::string("\xe0\x80", 2),      /* truncated three-byte sequence */
		std::string("\xed\xa0\x80", 3),  /* a surrogate, which UTF-8 excludes */
	};
	for (const std::string &value : bad) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar_n(
			doc, value.data(), value.size(), nullptr, nullptr);
		gtext_yaml_document_set_root(doc, root);
		Written w = write_doc(doc);
		gtext_yaml_free(doc);
		EXPECT_NE(w.status, GTEXT_YAML_OK)
			<< "wrote " << w.text << " for " << value.size()
			<< " bytes that are not UTF-8";
	}
}

/* 6.9.2: ns-anchor-char is ns-char minus c-flow-indicator.  The writer used
   to emit "&" and whatever string it held, which for "a b" produced "&a b" -
   read back as the anchor "a" with the rest of the line as the value. */
TEST(YamlWriterContract, AnAnchorNameIsCheckedBeforeItIsWritten) {
	const char *refused[] = { "", "a b", "a\nb", "a\tb", "a[b", "a]b",
		"a{b", "a}b", "a,b", "a\x7f" "b" };
	for (const char *name : refused) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root =
			gtext_yaml_node_new_scalar(doc, "v", nullptr, name);
		gtext_yaml_document_set_root(doc, root);
		EXPECT_NE(write_doc(doc).status, GTEXT_YAML_OK)
			<< "anchor <<" << name << ">> was written anyway";
		gtext_yaml_free(doc);
	}

	/* And everything the production does allow is written and read back.
	   Nine of these - ! " # % & ' * | > - were refused by the *parser* at the
	   first position of a name until the writer started producing them. */
	const char *accepted[] = { "a", "a!b", "!x", "\"x", "#x", "%x", "&x",
		"'x", "*x", "|x", ">x", ":x", "-x", "?x", "@x", "a:b" };
	for (const char *name : accepted) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root =
			gtext_yaml_node_new_scalar(doc, "v", nullptr, name);
		gtext_yaml_document_set_root(doc, root);
		Written w = write_doc(doc);
		gtext_yaml_free(doc);
		ASSERT_EQ(w.status, GTEXT_YAML_OK) << "anchor <<" << name << ">>";

		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		GTEXT_YAML_Document *back =
			gtext_yaml_parse(w.text.data(), w.text.size(), &opts, &err);
		ASSERT_NE(back, nullptr) << "anchor <<" << name << ">> wrote "
			<< w.text << " which this parser refuses: "
			<< (err.message ? err.message : "");
		gtext_yaml_error_free(&err);
		const char *got = gtext_yaml_node_anchor(gtext_yaml_document_root(back));
		EXPECT_STREQ(got ? got : "", name);
		gtext_yaml_free(back);
	}
}

/* A verbatim tag is used exactly as written (5.3), so the reader does not
   decode its escapes and the writer must not add any.  It used to encode "%"
   unconditionally, and with no matching decode a tag grew a layer of escaping
   on every round trip: "!a%21b" -> "!a%2521b" -> "!a%252521b". */
TEST(YamlWriterContract, ATagDoesNotGrowOnEveryRoundTrip) {
	const char *tags[] = {
		"!local", "!!str", "tag:example.com,2000:app/foo",
		"!a%21b", "!a%2521b", "tag:e.com,2000:100%25",
	};
	for (const char *tag : tags) {
		std::string current = tag;
		for (int pass = 0; pass < 3; pass++) {
			GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
			GTEXT_YAML_Node *root =
				gtext_yaml_node_new_scalar(doc, "v", current.c_str(), nullptr);
			gtext_yaml_document_set_root(doc, root);
			Written w = write_doc(doc);
			gtext_yaml_free(doc);
			ASSERT_EQ(w.status, GTEXT_YAML_OK) << tag;

			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(w.text.data(), w.text.size(), &opts, &err);
			ASSERT_NE(back, nullptr) << tag << " wrote " << w.text;
			gtext_yaml_error_free(&err);
			const char *got = gtext_yaml_node_tag(gtext_yaml_document_root(back));
			ASSERT_NE(got, nullptr) << tag;
			EXPECT_EQ(std::string(got), std::string(tag))
				<< "pass " << pass << " wrote " << w.text;
			current = got;
			gtext_yaml_free(back);
		}
	}
}

/* A tag that is not ns-uri-char+ has no spelling at all, so it is refused
   rather than approximated - and so is one in the "tag:yaml.org,2002:"
   namespace naming a type the spec does not define, since that namespace is
   not the author's to extend and the parser refuses "!!bogus" whatever the
   options say. The DOM API validates neither, so both reach the writer. */
TEST(YamlWriterContract, ATagThatCannotBeSpelledIsRefused) {
	const char *refused[] = { "!a b", "!a\nb", "tag:e.com,2000:caf\xc3\xa9",
		"!a\"b", "!a%zzb", "!a{b",
		"!!bogus", "!!-.#", "!!", "tag:yaml.org,2002:bogus" };
	for (const char *tag : refused) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *root =
			gtext_yaml_node_new_scalar(doc, "v", tag, nullptr);
		gtext_yaml_document_set_root(doc, root);
		EXPECT_NE(write_doc(doc).status, GTEXT_YAML_OK)
			<< "tag <<" << tag << ">> was written anyway";
		gtext_yaml_free(doc);
	}
}

/* "\0" is an escape 5.7 defines, so a scalar may hold a NUL.  The DOM has
   always kept a length; until gtext_yaml_node_scalar_length() there was no
   way to read past the NUL, and no constructor that could put one there. */
TEST(YamlWriterContract, AScalarMayHoldANul) {
	const std::string value("a\0b\0", 4);
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
	GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar_n(
		doc, value.data(), value.size(), nullptr, nullptr);
	ASSERT_NE(root, nullptr);
	gtext_yaml_document_set_root(doc, root);
	EXPECT_EQ(gtext_yaml_node_scalar_length(root), value.size());

	Written w = write_doc(doc);
	gtext_yaml_free(doc);
	ASSERT_EQ(w.status, GTEXT_YAML_OK);
	std::string back;
	ASSERT_TRUE(read_back_scalar(w.text, &back)) << w.text;
	EXPECT_EQ(back, value);
}

namespace {

/* The path a caller of the event API uses: the streaming parser straight into
 * the streaming writer.  Every other measurement of the writers reaches them
 * through a DOM, and a DOM walk reports no directives, no comments, and a tag
 * already resolved - so nothing could see the directives being dropped. */
struct Pipe {
	GTEXT_YAML_Writer *writer;
	GTEXT_YAML_Status status;
};

GTEXT_YAML_Status pipe_event(GTEXT_YAML_Stream *s, const void *ev, void *user) {
	(void)s;
	Pipe *p = (Pipe *)user;
	if (p->status != GTEXT_YAML_OK) return p->status;
	p->status = gtext_yaml_writer_event(p->writer, (const GTEXT_YAML_Event *)ev);
	return p->status;
}

bool pipe_through(const std::string &in, std::string *out) {
	GTEXT_YAML_Sink sink;
	if (gtext_yaml_sink_buffer(&sink) != GTEXT_YAML_OK) return false;
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	Pipe pipe{ gtext_yaml_writer_new(sink, &wopts), GTEXT_YAML_OK };
	if (!pipe.writer) { gtext_yaml_sink_buffer_free(&sink); return false; }

	GTEXT_YAML_Parse_Options popts = gtext_yaml_parse_options_default();
	GTEXT_YAML_Stream *st = gtext_yaml_stream_new(&popts, pipe_event, &pipe);
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(st, in.data(), in.size());
	if (status == GTEXT_YAML_OK) status = gtext_yaml_stream_finish(st);
	if (status == GTEXT_YAML_OK) status = pipe.status;
	if (status == GTEXT_YAML_OK) status = gtext_yaml_writer_finish(pipe.writer);
	if (status == GTEXT_YAML_OK) {
		out->assign(gtext_yaml_sink_buffer_data(&sink),
			gtext_yaml_sink_buffer_size(&sink));
	}
	gtext_yaml_stream_free(st);
	gtext_yaml_writer_free(pipe.writer);
	gtext_yaml_sink_buffer_free(&sink);
	return status == GTEXT_YAML_OK;
}

}  // namespace

/* The streaming writer used to answer a DIRECTIVE event with OK and write
   nothing.  The event stream reports a tag as it was written rather than
   resolved, so "!e!foo" arrives still spelled with its handle; dropping the
   "%TAG !e! ..." that declared the handle left a document this very parser
   refuses. */
TEST(YamlWriterContract, TheStreamingWriterKeepsTheDirectives) {
	const std::string in =
		"%TAG !e! tag:example.com,2000:app/\n"
		"---\n"
		"!e!foo v\n";
	std::string out;
	ASSERT_TRUE(pipe_through(in, &out));
	EXPECT_NE(out.find("%TAG !e! tag:example.com,2000:app/"), std::string::npos)
		<< "wrote: " << out;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	GTEXT_YAML_Document *back =
		gtext_yaml_parse(out.data(), out.size(), &opts, &err);
	ASSERT_NE(back, nullptr) << "wrote " << out << " which this parser refuses: "
		<< (err.message ? err.message : "");
	gtext_yaml_error_free(&err);
	const char *tag = gtext_yaml_node_tag(gtext_yaml_document_root(back));
	ASSERT_NE(tag, nullptr);
	EXPECT_STREQ(tag, "tag:example.com,2000:app/foo");
	gtext_yaml_free(back);
}

/* ...and a named handle is writable only where this writer declared it.

   c-ns-shorthand-tag is a handle followed by ns-tag-char+ (6.8.2). The
   primary "!" and the secondary "!!" are defined for every document; a named
   "!e!" means whatever a %TAG declared and means nothing at all where none
   did. The DOM writer emits no directives, so a named handle is undeclared
   there by construction - and "!a!3" was going out as itself, leaving a
   document this parser refuses for a handle no %TAG defined.

   There is nothing to fall back on. "!<!a!3>" is a *different* tag - the
   literal URI, not the handle's prefix followed by "3" - and a prefix the
   writer invented would be worse than a refusal. So it is refused, the way an
   unwritable anchor and "!!bogus" already are. The test above is the other
   half of this rule: declare the handle and the same shorthand writes. */
TEST(YamlWriterContract, ANamedTagHandleIsRefusedWhereNoTagDeclaredIt) {
	struct Case { const char *tag; bool writable; };
	const Case cases[] = {
		{ "!a!3", false },   /* the fuzzer's find */
		{ "!a!3x", false },
		{ "!e!foo", false },
		{ "!", true },       /* the primary handle, always defined */
		{ "!local", true },
		{ "!!str", true },   /* the secondary, likewise */
	};
	for (const Case &c : cases) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		GTEXT_YAML_Node *node =
			gtext_yaml_node_new_scalar(doc, "v", c.tag, nullptr);
		ASSERT_NE(node, nullptr) << c.tag;
		gtext_yaml_document_set_root(doc, node);
		Written w = write_doc(doc);
		EXPECT_EQ(w.status == GTEXT_YAML_OK, c.writable)
			<< "tag " << c.tag << " wrote " << w.text;

		/* And what it does write has to read back, which is the property
		   that failed: the bytes were fine, the handle was not. */
		if (w.status == GTEXT_YAML_OK) {
			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(w.text.c_str(), w.text.size(), &opts, &err);
			EXPECT_NE(back, nullptr) << "tag " << c.tag << " wrote " << w.text
				<< " which this parser refuses: "
				<< (err.message ? err.message : "");
			gtext_yaml_error_free(&err);
			if (back) gtext_yaml_free(back);
		}
		gtext_yaml_free(doc);
	}
}

/* A %TAG declares its handle for the document it precedes and no further
   (6.8.2), so the same shorthand in a second document is undeclared again. */
TEST(YamlWriterContract, ADeclaredHandleDoesNotCarryToTheNextDocument) {
	std::string out;
	ASSERT_TRUE(pipe_through(
		"%TAG !e! tag:example.com,2000:app/\n---\n!e!foo v\n", &out));
	EXPECT_NE(out.find("%TAG !e!"), std::string::npos) << "wrote: " << out;

	/* Two documents, the handle declared only for the first: the second's
	   shorthand has no declaration and the writer must not write it. */
	std::string ignored;
	EXPECT_FALSE(pipe_through(
		"%TAG !e! tag:example.com,2000:app/\n---\n!e!foo v\n"
		"--- !e!bar w\n", &ignored))
		<< "wrote: " << ignored;
}

/* %YAML travels the same way, and has to survive the trip rather than being
   quietly dropped. */
TEST(YamlWriterContract, TheStreamingWriterKeepsTheVersionDirective) {
	std::string out;
	ASSERT_TRUE(pipe_through("%YAML 1.2\n---\nv\n", &out));
	EXPECT_NE(out.find("%YAML 1.2"), std::string::npos) << "wrote: " << out;

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	GTEXT_YAML_Document *back =
		gtext_yaml_parse(out.data(), out.size(), &opts, &err);
	ASSERT_NE(back, nullptr) << "wrote " << out;
	gtext_yaml_error_free(&err);
	gtext_yaml_free(back);
}

/* The two event APIs look like two ends of a pipe and are not one.
 *
 * gtext_yaml_writer_event() takes *composed* events - MAPPING_START, the
 * pairs, MAPPING_END - which is what gtext_yaml_stream_walk() produces from a
 * parsed document. The streaming *parser* reports structure as it is written
 * instead: the ":" of a block mapping, the "-" of a block sequence and even
 * the "," between two flow entries arrive as GTEXT_YAML_EVENT_INDICATOR, and
 * composing them is left to the consumer - which is what the DOM parser is.
 * A lone scalar is the whole of what crosses unchanged.
 *
 * The writer used to answer an indicator with OK and write nothing, so a
 * caller who joined the two got no error and a document with its block
 * structure gone: "a: 1" over "b: 2" came back as the one scalar "a1b2".
 * There is nothing to render and no way to guess what was meant, so it is
 * refused - loudly, at the first event that cannot be written, rather than
 * quietly at every one of them. */
TEST(YamlWriterContract, AnIndicatorEventIsRefusedRatherThanIgnored) {
	std::string out;
	EXPECT_FALSE(pipe_through("a: 1\nb: 2\n", &out))
		<< "the block mapping was accepted and written as: " << out;
	EXPECT_FALSE(pipe_through("- a\n- b\n", &out))
		<< "the block sequence was accepted and written as: " << out;

	/* A flow collection does open with SEQUENCE_START, but its separators
	   are indicators too, so it gets no further. */
	EXPECT_FALSE(pipe_through("[a, b]\n", &out))
		<< "the flow sequence was accepted and written as: " << out;

	/* A lone scalar carries no indicator, which is the whole of what does go
	   through - and why the directive tests above are written with one. */
	ASSERT_TRUE(pipe_through("v\n", &out));
	EXPECT_NE(out.find("v"), std::string::npos) << "wrote: " << out;
}

/* 5.1 again, from the other side.
 *
 * The gate that enforces c-printable runs over the decoded character stream
 * as bytes arrive, and a sequence a feed cut in half is held rather than
 * judged from its first byte - which is right while more input may come. At
 * the end of the stream it was still being held, and skipped, on the grounds
 * that gtext_utf8_validate() would catch it when the scalar was assembled.
 * Bytes on a directive or a comment line never become a scalar, so nothing
 * ever did: "%" followed by a lone 0xC2 was a document, and the same bytes
 * with a line break after them were not.
 *
 * Found by feeding the parser's own events to the writer: the writer emitted
 * the directive back, and the parser then refused what it had just accepted.
 *
 * spec-1.2.2.corpus cannot hold this case - it spells "\xNN" as a code point,
 * which is the whole difference here - so it lives with the writer. */
TEST(YamlWriterContract, ATruncatedSequenceIsNotACharacter) {
	struct Case { const char *label; const char *bytes; size_t len; };
	const Case cases[] = {
		{ "a directive line",            "%\xc2",      2 },
		{ "a directive line, terminated", "%\xc2\n",   3 },
		{ "a named directive",           "%YAML\xc2",  6 },
		{ "a comment line",              "#\xc2",      2 },
		{ "a comment line, terminated",  "#\xc2\n",    3 },
		{ "a plain scalar",              "a\xc2",      2 },
		{ "a stray continuation byte",   "%\x80",      2 },
		{ "an invalid lead byte",        "%\xff",      2 },
	};
	for (const Case &c : cases) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(c.bytes, c.len, &opts, &err);
		EXPECT_EQ(doc, nullptr) << c.label << " was accepted";
		if (doc) gtext_yaml_free(doc);
		gtext_yaml_error_free(&err);
	}
}
