/**
 * @file yaml_render.h
 * @brief One spelling of a parsed document, shared by the YAML table tests.
 *
 * Render() parses and prints the DOM in a compact JSON-like form, and
 * returns "" when the parse is refused - so a table of inputs can carry
 * both the answers and the refusals. It was copied into seven test files
 * before this header existed, which is how the alias case came to be
 * missing from the one that needed it.
 *
 * It prints an alias by printing its target, so a document that names an
 * enclosing collection - "&O [1, *O]", and every block spelling of it since
 * adopt_own_line_anchor() started re-pointing the anchor - would recur until
 * the stack ran out. A table test written in good faith would look like a
 * mystery crash rather than a diff, so a node already on the path prints as
 * "*" and the walk stops there.
 */
#ifndef GTEXT_TESTS_YAML_RENDER_H
#define GTEXT_TESTS_YAML_RENDER_H

#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include <ghoti.io/text/yaml.h>

namespace {

/* The nodes between the root and the one being printed, so a recursive
   document terminates. Only the path is held, not every node seen: a node
   reached twice down two different branches is not a cycle, and printing it
   once each time is what a reader of the expectation wants. */
std::vector<const GTEXT_YAML_Node *> &RenderPath() {
	static std::vector<const GTEXT_YAML_Node *> path;
	return path;
}

struct RenderFrame {
	explicit RenderFrame(const GTEXT_YAML_Node *n) { RenderPath().push_back(n); }
	~RenderFrame() { RenderPath().pop_back(); }
};

bool RenderOnPath(const GTEXT_YAML_Node *n) {
	for (const GTEXT_YAML_Node *seen : RenderPath()) {
		if (seen == n) return true;
	}
	return false;
}

void RenderInto(const GTEXT_YAML_Node *n, std::string &out) {
	if (!n) { out += "<null-node>"; return; }
	if (RenderOnPath(n)) { out += '*'; return; }
	RenderFrame frame(n);
	switch (gtext_yaml_node_type(n)) {
	case GTEXT_YAML_NULL:
		out += "null";
		return;
	case GTEXT_YAML_BOOL:
	case GTEXT_YAML_INT:
	case GTEXT_YAML_FLOAT: {
		const char *s = gtext_yaml_node_as_string(n);
		out += s ? s : "<none>";
		return;
	}
	case GTEXT_YAML_STRING: {
		const char *s = gtext_yaml_node_as_string(n);
		out += '"';
		for (; s && *s; ++s) {
			if (*s == '\n') out += "\\n";
			else if (*s == '\t') out += "\\t";
			else if (*s == '"') out += "\\\"";
			else if (*s == '\\') out += "\\\\";
			else out += *s;
		}
		out += '"';
		return;
	}
	case GTEXT_YAML_SEQUENCE:
	case GTEXT_YAML_SET:
	case GTEXT_YAML_OMAP:
	case GTEXT_YAML_PAIRS: {
		out += '[';
		const size_t len = gtext_yaml_sequence_length(n);
		for (size_t i = 0; i < len; ++i) {
			if (i) out += ", ";
			RenderInto(gtext_yaml_sequence_get(n, i), out);
		}
		out += ']';
		return;
	}
	case GTEXT_YAML_MAPPING: {
		out += '{';
		const size_t len = gtext_yaml_mapping_size(n);
		for (size_t i = 0; i < len; ++i) {
			const GTEXT_YAML_Node *k = nullptr, *v = nullptr;
			gtext_yaml_mapping_get_at(n, i, &k, &v);
			if (i) out += ", ";
			RenderInto(k, out);
			out += ": ";
			RenderInto(v, out);
		}
		out += '}';
		return;
	}
	case GTEXT_YAML_ALIAS: {
		/* The DOM keeps the alias node and records what it resolved to, so
		   print the target: "*b" as a key is whatever &b named. */
		const GTEXT_YAML_Node *target = gtext_yaml_alias_target(n);
		if (target) RenderInto(target, out);
		else out += "<unresolved-alias>";
		return;
	}
	default:
		out += "<other>";
		return;
	}
}

std::string Render(const char *input) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(input, strlen(input), nullptr, &err);
	if (!doc) return "";
	std::string out;
	RenderInto(gtext_yaml_document_root(doc), out);
	gtext_yaml_free(doc);
	return out;
}

}  // namespace

#endif  /* GTEXT_TESTS_YAML_RENDER_H */
