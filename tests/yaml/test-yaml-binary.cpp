/**
 * @file test-yaml-binary.cpp
 * @brief Tests for !!binary tag support.
 */

#include <gtest/gtest.h>
#include <string>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static std::string write_doc(
	const GTEXT_YAML_Document *doc,
	const GTEXT_YAML_Write_Options *opts
) {
	GTEXT_YAML_Sink sink;
	GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	status = gtext_yaml_write_document(doc, &sink, opts);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	std::string output = gtext_yaml_sink_buffer_data(&sink);
	gtext_yaml_sink_buffer_free(&sink);
	return output;
}

TEST(YamlBinary, DecodeValidBase64) {
	const char *yaml = "!!binary SGVsbG8=";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const unsigned char *data = NULL;
	size_t len = 0;
	EXPECT_TRUE(gtext_yaml_node_as_binary(root, &data, &len));
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(len, 5u);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(data), len), "Hello");
	EXPECT_STREQ(gtext_yaml_node_as_string(root), "SGVsbG8=");

	gtext_yaml_free(doc);
}

TEST(YamlBinary, DecodeIgnoresWhitespace) {
	const char *yaml = "!!binary \"SGVs\n bG8=\"";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const unsigned char *data = NULL;
	size_t len = 0;
	EXPECT_TRUE(gtext_yaml_node_as_binary(root, &data, &len));
	EXPECT_EQ(len, 5u);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(data), len), "Hello");

	gtext_yaml_free(doc);
}

TEST(YamlBinary, DecodeRejectsInvalid) {
	const char *yaml = "!!binary SGVsbG8";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

/* Base64 of no bytes is the empty string, so an empty !!binary value is an
   empty byte string - which is what PyYAML gives back, and its type
   repository is where !!binary is defined. It was refused, and that made the
   writer's own output unreadable: an empty binary node is written
   '!!binary ""'. The writer fuzzer is what noticed; no corpus of YAML text
   has an empty binary in it.

   White space alone decodes the same way, because the filter drops it before
   anything counts. "a" and "aGk" stay invalid: base64 comes in groups of
   four. */
TEST(YamlBinary, DecodesAnEmptyValueToNoBytes) {
	const char *documents[] = {
		"!!binary \"\"", "!!binary", "!!binary \"  \"", "!!binary ''",
	};
	for (const char *yaml : documents) {
		GTEXT_YAML_Error err = {};
		GTEXT_YAML_Document *doc =
			gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
		ASSERT_NE(doc, nullptr) << yaml << ": "
			<< (err.message ? err.message : "");
		const unsigned char *data = nullptr;
		size_t len = 1;
		EXPECT_TRUE(gtext_yaml_node_as_binary(
			gtext_yaml_document_root(doc), &data, &len)) << yaml;
		EXPECT_EQ(len, (size_t)0) << yaml;
		gtext_yaml_free(doc);
	}

	/* And what the writer produces for one reads back the same way, which is
	   the property that failed. */
	GTEXT_YAML_Error err = {};
	const char *src = "!!binary \"\"";
	GTEXT_YAML_Document *doc = gtext_yaml_parse(src, strlen(src), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
	ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &wopts), GTEXT_YAML_OK);
	const std::string out(gtext_yaml_sink_buffer_data(&sink),
		gtext_yaml_sink_buffer_size(&sink));
	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);

	GTEXT_YAML_Error err2 = {};
	GTEXT_YAML_Document *back =
		gtext_yaml_parse(out.data(), out.size(), NULL, &err2);
	ASSERT_NE(back, nullptr) << "wrote " << out << " which this parser refuses: "
		<< (err2.message ? err2.message : "");
	gtext_yaml_free(back);
}

/* A tag is an assertion about the value, and "!!binary" is one that can be
   false. The constructor took any text at all and never decoded it, so a node
   built from perfectly good base64 answered false to as_binary() where the
   same document parsed answers with the bytes - and text that is not base64
   was taken all the same, then written after the tag: "(((" went out as
   '!!binary (((' and this parser refused the writer's own output.

   It is checked on the way in now, the way the parser checks it, and the node
   carries the decoded bytes either way. */
TEST(YamlBinary, TheConstructorDecodesAndRefusesWhatIsNotBase64) {
	struct Case { const char *text; bool ok; size_t len; };
	const Case cases[] = {
		{ "aGk=", true, 2 },
		{ "", true, 0 },
		{ "SGVsbG8=", true, 5 },
		{ "(((", false, 0 },
		{ "hi", false, 0 },
		{ "a", false, 0 },
		{ "SGVsbG8", false, 0 },
	};
	for (const Case &c : cases) {
		GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
		ASSERT_NE(doc, nullptr);
		GTEXT_YAML_Node *node =
			gtext_yaml_node_new_scalar(doc, c.text, "!!binary", nullptr);
		EXPECT_EQ(node != nullptr, c.ok) << "text <<" << c.text << ">>";
		if (node) {
			const unsigned char *data = nullptr;
			size_t len = 99;
			EXPECT_TRUE(gtext_yaml_node_as_binary(node, &data, &len))
				<< "text <<" << c.text << ">>";
			EXPECT_EQ(len, c.len) << "text <<" << c.text << ">>";

			/* And what the writer makes of it reads back. */
			gtext_yaml_document_set_root(doc, node);
			GTEXT_YAML_Sink sink;
			ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
			GTEXT_YAML_Write_Options wopts = gtext_yaml_write_options_default();
			ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &wopts),
				GTEXT_YAML_OK);
			const std::string out(gtext_yaml_sink_buffer_data(&sink),
				gtext_yaml_sink_buffer_size(&sink));
			gtext_yaml_sink_buffer_free(&sink);
			GTEXT_YAML_Error err = {};
			GTEXT_YAML_Document *back =
				gtext_yaml_parse(out.data(), out.size(), nullptr, &err);
			EXPECT_NE(back, nullptr) << "wrote " << out
				<< " which this parser refuses: "
				<< (err.message ? err.message : "");
			if (back) gtext_yaml_free(back);
		}
		gtext_yaml_free(doc);
	}

	/* The spelled-out tag says the same thing. */
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
	EXPECT_EQ(gtext_yaml_node_new_scalar(
		doc, "(((", "tag:yaml.org,2002:binary", nullptr), nullptr);
	gtext_yaml_free(doc);
}

TEST(YamlBinary, WriterEmitsCanonicalBase64) {
	const char *yaml = "!!binary SGVsbG8=";
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

	std::string output = write_doc(doc, NULL);
	EXPECT_EQ(output, "!!binary SGVsbG8=");

	gtext_yaml_free(doc);
}

/* The scalar's own text is left as it was written.
 *
 * It used to be replaced by a canonical re-encoding of the decoded bytes,
 * which threw away the line breaks the author had put in - base64 in a
 * literal block scalar is written in short lines on purpose. Suite case 565N
 * carries such a value and expects it back unchanged. Re-encoding also
 * quietly rewrites input that decodes to the same bytes but was not spelled
 * the same way, which is not a parser's business.
 *
 * The decoded bytes are still there; that is what gtext_yaml_node_as_binary()
 * is for. */
TEST(YamlBinary, KeepsTheTextAsWritten) {
	const char *yaml =
		"g: !!binary |\n"
		" R0lGODlhDAAM\n"
		" AIQAAP//9/X1\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *v = gtext_yaml_mapping_get(root, "g");
	ASSERT_NE(v, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(v), "R0lGODlhDAAM\nAIQAAP//9/X1\n");

	/* And the bytes are still decoded. */
	const unsigned char *data = nullptr;
	size_t len = 0;
	EXPECT_TRUE(gtext_yaml_node_as_binary(v, &data, &len));
	EXPECT_EQ(len, (size_t)18);

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
