/**
 * Where a folded scalar may be broken, and where it may not.
 *
 * A folded block scalar reads a single line break as a space (8.1.3), which
 * is what lets the writer break a long line at a space and get the space back
 * on the next parse. The exception is a *more-indented* line: 6.5 keeps the
 * break before one rather than folding it, so a break that leaves white space
 * against it is not reversible. More-indented means beginning with a space
 * **or a tab**, and the writer only checked for a space.
 *
 * So "three \tfour", broken at its space, went out as a line ending "three"
 * and a line beginning with a tab; the reader kept that break as a line feed,
 * and the space the writer had spent was gone. A space became a newline in a
 * document that had only been written out and read back.
 *
 * Found by the writer fuzzer (artifact
 * crash-7a03dc4637822706d28f2aef56463d1483d00ba0), minimised away from it -
 * the artifact reached it at indent 13 in UTF-16BE with a BOM, none of which
 * the defect needed.
 *
 * The planner above write_folded_line() already refuses a scalar whose own
 * lines begin or end with white space. The bug was that the same rule was not
 * applied to the lines the writer invents.
 */
#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <string.h>
}

#include <string>
#include <vector>

namespace {

std::string WriteFolded(const std::string &value, int line_width) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
	EXPECT_NE(doc, nullptr);
	if (!doc) return "";
	GTEXT_YAML_Node *node = gtext_yaml_node_new_scalar_n(
		doc, value.c_str(), value.size(), nullptr, nullptr);
	EXPECT_NE(node, nullptr);
	/* The style has to be on the node. opts.scalar_style is the default for
	   nodes that do not carry one, and a scalar built by the DOM API does
	   carry one - plain - which the chooser then re-decides on its merits. */
	EXPECT_TRUE(gtext_yaml_node_set_scalar_style(
		node, GTEXT_YAML_SCALAR_STYLE_FOLDED));
	/* Under a mapping key, because a block scalar ends its own line and the
	   writer will not put one where a flow context is in force. */
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
	EXPECT_NE(map, nullptr);
	GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "k", nullptr, nullptr);
	EXPECT_NE(key, nullptr);
	/* Returns the mapping to use from here on, which need not be the one
	   passed in - the same shape as gtext_yaml_sequence_append(). */
	map = gtext_yaml_mapping_set(doc, map, key, node);
	EXPECT_NE(map, nullptr);
	gtext_yaml_document_set_root(doc, map);

	GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
	opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
	opts.line_width = line_width;
	/* A block scalar ends its own line, so there is nowhere inside a flow
	   collection to put one; left on AUTO a two-node mapping goes flow and
	   the value comes out double-quoted. */
	opts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;

	GTEXT_YAML_Sink sink;
	EXPECT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);
	EXPECT_EQ(gtext_yaml_write_document(doc, &sink, &opts), GTEXT_YAML_OK);
	std::string out(
		gtext_yaml_sink_buffer_data(&sink), gtext_yaml_sink_buffer_size(&sink));
	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
	return out;
}

/* Reads a written document back and returns its root scalar. */
bool ReadBackScalar(const std::string &text, std::string *out) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(text.c_str(), text.size(), nullptr, &err);
	if (!doc) return false;
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *k = nullptr, *v = nullptr;
	gtext_yaml_mapping_get_at(root, 0, &k, &v);
	const char *s = v ? gtext_yaml_node_as_string(v) : nullptr;
	if (s) out->assign(s, gtext_yaml_node_scalar_length(v));
	gtext_yaml_free(doc);
	return s != nullptr;
}

/* The body lines of the written block scalar, with the block's own
   indentation removed.
   
   The indentation is measured from the first non-empty body line rather than
   assumed: it is whatever the writer chose, 8.1.1.1 says auto-detection reads
   it from exactly that line, and an earlier draft of this helper hardcoded it
   and reported every line as more-indented. */
std::vector<std::string> BodyLines(const std::string &written) {
	std::vector<std::string> lines;
	size_t pos = written.find('\n');
	if (pos == std::string::npos) return lines;
	pos++;

	std::vector<std::string> raw;
	while (pos <= written.size()) {
		size_t end = written.find('\n', pos);
		if (end == std::string::npos) end = written.size();
		raw.push_back(written.substr(pos, end - pos));
		if (end == written.size()) break;
		pos = end + 1;
	}

	size_t base = std::string::npos;
	for (const std::string &line : raw) {
		if (line.find_first_not_of(" \t") == std::string::npos) continue;
		base = line.find_first_not_of(' ');
		break;
	}
	if (base == std::string::npos) return lines;

	for (const std::string &line : raw) {
		if (line.find_first_not_of(" \t") == std::string::npos) continue;
		lines.push_back(line.size() >= base ? line.substr(base) : std::string());
	}
	return lines;
}

}  // namespace

/* The control. If the writer never folds this value there is no break to get
   wrong, and every assertion below would hold vacuously. */
TEST(YamlFoldedFoldPoints, TheValueIsLongEnoughToBeFolded) {
	const std::string value = "one two three \tfourfivesix";
	const std::string written = WriteFolded(value, 20);
	EXPECT_NE(written.find(">"), std::string::npos)
		<< "not written as a folded scalar at all: " << written;
	EXPECT_GE(BodyLines(written).size(), 2u)
		<< "the writer did not break the line, so there is no fold to test:\n"
		<< written;
}

/* The defect itself, stated as the property that matters. */
TEST(YamlFoldedFoldPoints, ASpaceBeforeATabSurvivesTheRoundTrip) {
	const std::string value = "one two three \tfourfivesix";
	const std::string written = WriteFolded(value, 20);
	std::string back;
	ASSERT_TRUE(ReadBackScalar(written, &back)) << written;
	EXPECT_EQ(back, value)
		<< "written as:\n" << written;
}

/* And the reason it survives: no line the writer invented begins with white
   space. Asserting the shape as well as the value, because a round trip can
   come out right for a compensating reason. */
TEST(YamlFoldedFoldPoints, NoInventedLineBeginsWithWhiteSpace) {
	const char *values[] = {
		"one two three \tfourfivesix",
		"alpha beta \t gamma deltaepsilonzeta",
		"a b \tc d \te f \tg hhhhhhhhhhhhhhhh",
		"padded  \t  words here and some more words to force a fold",
	};
	for (const char *v : values) {
		for (int width = 4; width <= 24; width += 4) {
			const std::string written = WriteFolded(v, width);
			for (const std::string &line : BodyLines(written)) {
				if (line.empty()) continue;
				EXPECT_FALSE(line[0] == ' ' || line[0] == '\t')
					<< "value \"" << v << "\" at width " << width
					<< " produced a more-indented line:\n" << written;
			}
			std::string back;
			ASSERT_TRUE(ReadBackScalar(written, &back))
				<< "value \"" << v << "\" at width " << width << ":\n"
				<< written;
			EXPECT_EQ(back, std::string(v))
				<< "at width " << width << ", written as:\n" << written;
		}
	}
}
