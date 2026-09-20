/**
 * How the caller splits the input must not change what the parser says.
 *
 * gtext_yaml_stream_feed() promises exactly that - "the parser will buffer
 * internally if a token or block scalar spans multiple calls" - and it was
 * not keeping the promise. Every lookahead in the plain-scalar scanner read
 * -1 when it ran off the end of the bytes that had arrived so far, and every
 * one of them treated that as "the token ends here". So
 *
 *     name: Mark McGwire
 *
 * fed one byte at a time came back as the two scalars "Mark" and "McGwire":
 * the space was judged a terminator by a lookahead that had simply run out
 * of buffer. Whether a document parsed correctly depended on where the
 * caller's chunk boundaries happened to fall - on socket timing, on a read
 * size, on nothing to do with the document.
 *
 * The second half was properties. The scanner hands "&" back as one token
 * and the name after it as the next, and it consumes destructively, so a
 * caller that took the "&" and found no name behind it yet had nowhere to
 * put the "&" back. It was dropped, and the name arrived later looking like
 * an ordinary scalar: "First occurrence: &anchor Foo" lost its anchor and
 * gained a scalar "anchor". Neither half is taken now until both are there.
 *
 * What this pins is the invariant, not the event stream: for each document,
 * feeding it whole and feeding it in n-byte pieces must produce the same
 * events. A future change that improves the parse improves both sides at
 * once.
 */
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <string.h>
}

namespace {

/* Render the event stream a reader produces for one input, fed in chunks of
 * `chunk` bytes (0 meaning "all at once"). */
std::string Events(const std::string &input, size_t chunk) {
	std::ostringstream out;
	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
	if (!reader) return "(no reader)";

	auto drain = [&]() {
		for (;;) {
			GTEXT_YAML_Event ev;
			GTEXT_YAML_Error err;
			memset(&ev, 0, sizeof(ev));
			memset(&err, 0, sizeof(err));
			if (gtext_yaml_reader_next(reader, &ev, &err) != GTEXT_YAML_OK) break;
			out << "[" << (int)ev.type;
			if (ev.type == GTEXT_YAML_EVENT_SCALAR) {
				out << " '" << std::string(ev.data.scalar.ptr, ev.data.scalar.len) << "'";
			} else if (ev.type == GTEXT_YAML_EVENT_INDICATOR) {
				out << " '" << ev.data.indicator << "'";
			} else if (ev.type == GTEXT_YAML_EVENT_ALIAS && ev.data.alias_name) {
				out << " *" << ev.data.alias_name;
			}
			if (ev.anchor) out << " &" << ev.anchor;
			if (ev.tag) out << " <" << ev.tag << ">";
			out << "]";
			if (ev.type == GTEXT_YAML_EVENT_STREAM_END) break;
		}
	};

	size_t off = 0;
	bool failed = false;
	while (off < input.size()) {
		const size_t n = chunk ? std::min(chunk, input.size() - off)
		                       : input.size() - off;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		if (gtext_yaml_reader_feed(reader, input.data() + off, n, &err)
				!= GTEXT_YAML_OK) {
			out << "{err}";
			failed = true;
			break;
		}
		drain();
		off += n;
	}
	if (!failed) {
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		if (gtext_yaml_reader_feed(reader, nullptr, 0, &err) != GTEXT_YAML_OK) {
			out << "{err}";
		}
		drain();
	}
	gtext_yaml_reader_free(reader);
	return out.str();
}

/* Documents whose tokens straddle a chunk boundary in at least one of the
 * sizes below.  Block scalars are deliberately absent: their body is still
 * cut at a buffer boundary, which the notes in scanner.c record. */
const char *kDocuments[] = {
	"name: Mark McGwire\n",
	"a: one two three\n",
	"First occurrence: &anchor Foo\nSecond occurrence: *anchor\n",
	"- !!int 1\n- !!int -2\n- !!int 33\n",
	"--- !!map\n? a\n: b\n",
	"- !!str c\n",
	"key: a, b - c\n",
	"[a b, c d]\n",
	"{k: v w, j: x y}\n",
	"a: 1\nb: two words\nc: [x, y]\n",
	"? an explicit key\n: and its value\n",
	"quoted: \"some text here\"\n",
	"single: 'some text here'\n",
	"- &a x\n- *a\n",
	"!<tag:example.com,2000:thing> value\n",
	"top:\n  nested: deep value\n",
	"a: b#c\n",
	"plain: text\n  lines\n",
	"%YAML 1.2\n---\nvalue\n",
	"- a\n- b\n- c\n",

	/* No trailing newline.  The last token of these ends at the end of the
	 * input rather than at a break, so they are the ones that say the
	 * scanner still knows the difference between "the buffer ran out" and
	 * "the input ran out".  Asking for more when the input really is
	 * finished loses the last token outright: "a: b" became a key with no
	 * value, and "*a" became nothing at all. */
	"a: b",
	"&a x",
	"*a",
	"!!str x",
	"plain",
	"[&a,&b]",
	"- one two",

	/* And ending in trailing white space, or in a bare ":".  These reach the
	 * lookaheads through their "ran off the end" branch with the input
	 * genuinely finished, which is the case that says the scanner still
	 * tells "no more bytes yet" apart from "no more bytes ever". Asking for
	 * more here waits for input that is never coming, and the last scalar
	 * is simply never emitted. */
	"a: b ",
	"a: b  ",
	"a: b :",
	"a: b:",
	"- one two ",
};

} // namespace

TEST(YamlChunkedFeed, ChunkSizeDoesNotChangeTheParse) {
	for (const char *doc : kDocuments) {
		const std::string input(doc);
		const std::string whole = Events(input, 0);
		for (size_t chunk : {(size_t)1, (size_t)2, (size_t)3, (size_t)5, (size_t)7}) {
			EXPECT_EQ(Events(input, chunk), whole)
				<< "chunk size " << chunk << " changed the parse of "
				<< ::testing::PrintToString(input);
		}
	}
}

/* The two shapes that were wrong, named so a regression says which. */
TEST(YamlChunkedFeed, APlainScalarSurvivesAChunkBoundary) {
	const std::string input = "name: Mark McGwire\n";
	EXPECT_NE(Events(input, 0).find("'Mark McGwire'"), std::string::npos);
	EXPECT_EQ(Events(input, 1), Events(input, 0));
}

TEST(YamlChunkedFeed, AnAnchorSurvivesAChunkBoundary) {
	const std::string input = "First occurrence: &anchor Foo\n";
	EXPECT_NE(Events(input, 0).find("&anchor"), std::string::npos);
	EXPECT_EQ(Events(input, 1), Events(input, 0));
}

/* A document that ends without a line break still ends. */
TEST(YamlChunkedFeed, TheLastTokenSurvivesTheEndOfInput) {
	struct { const char *input; const char *wanted; } kCases[] = {
		{"a: b", "'b'"},
		{"*a", "*a"},
		{"&a x", "&a"},
		{"!!str x", "<!!str>"},
		{"plain", "'plain'"},
		{"a: b ", "'b'"},
		{"a: b:", "'b'"},
		{"- one two ", "'one two'"},
	};
	for (const auto &c : kCases) {
		const std::string whole = Events(c.input, 0);
		EXPECT_NE(whole.find(c.wanted), std::string::npos)
			<< "whole feed lost it: " << c.input << " -> " << whole;
		EXPECT_EQ(Events(c.input, 1), whole) << "input: " << c.input;
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
