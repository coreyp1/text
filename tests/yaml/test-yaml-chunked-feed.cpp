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
 * Block scalars needed a different answer. Their header is consumed before
 * the body is read, so a body that turns out to be incomplete has nothing to
 * go back to - the loop settled for the lines already in hand, and
 * "literal: |" over "  some" over "  text" came back as the single folded
 * line "some text". The whole block is checked for before any of it is taken.
 * So is the node after a bare "!", which is the only thing that says whether
 * the "!" was the non-specific tag or the start of a longer one.
 *
 * What this pins is the invariant, not the event stream: for each document,
 * feeding it whole and feeding it in n-byte pieces must produce the same
 * events. A future change that improves the parse improves both sides at
 * once.
 *
 * Two of the tests below ask a different question, because the first one
 * cannot see the answer. Deferring is always safe - a scanner that simply
 * waited for finish() and then parsed everything at once would satisfy every
 * comparison above and stream nothing at all, which is the one thing this
 * API is for. So they feed all but the last few bytes and ask what has come
 * out yet.
 */
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <chrono>

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
 * sizes below. */
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

	/* Block scalars.  These could not be fixed the way the others were: the
	 * header is consumed before the body is read, so a body that turns out
	 * to be incomplete has nothing to go back to, and the loop settled for
	 * the lines already in hand - "literal: |" over "  some" over "  text"
	 * came back as the single folded line "some text".  The whole block is
	 * checked for before any of it is taken. */
	"literal: |\n  some\n  text\n",
	"folded: >\n  some\n  text\n",
	"a: |+\n  x\n\n",
	"a: |-\n  x\n",
	"a: |2\n    x\n",
	"- |\n detected\n",
	"--- |\nabc\n...\n",
	"a: |\n  one\n\n  two\n",

	/* The non-specific tag is handed over on its own, and what it applies to
	 * is only known from the token after it - so that token has to have
	 * arrived as well, or the "!" is gone with nothing to show for it. */
	"---\n! a\n",
	"- ! 12\n",
	"!\nfoo\n",
	"a: !foo b\n",
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

/* The two shapes the block-scalar and tag halves were wrong in. */
TEST(YamlChunkedFeed, ABlockScalarSurvivesAChunkBoundary) {
	const std::string input = "literal: |\n  some\n  text\n";
	EXPECT_NE(Events(input, 0).find("'some\ntext\n'"), std::string::npos);
	EXPECT_EQ(Events(input, 1), Events(input, 0));
}

TEST(YamlChunkedFeed, ANonSpecificTagSurvivesAChunkBoundary) {
	const std::string input = "---\n! a\n";
	EXPECT_NE(Events(input, 0).find("<!>"), std::string::npos);
	EXPECT_EQ(Events(input, 1), Events(input, 0));
}

/* Events have to come out as the input arrives, not all at the end.
 *
 * Deferring is always safe - the scanner can ask for more input as often as
 * it likes and still be right once the stream is finished - so nothing about
 * *what* this parser emits can tell a parser that streams from one that
 * quietly buffers the whole document and emits it on finish(). The
 * difference is the whole point of the API: a caller feeding a large
 * document should not need it all in memory at once.
 *
 * So this asks a different question: with all but the last few bytes fed,
 * has the first block scalar been emitted yet? The checks that let the
 * scanner commit early - a line dedenting out of the block, a document
 * marker - are what make the answer yes. */
TEST(YamlChunkedFeed, EventsArriveBeforeTheInputEnds) {
	const std::string doc = "a: |\n  one\n  two\nb: |\n  three\n  four\n";
	const size_t hold_back = 6;
	ASSERT_GT(doc.size(), hold_back);

	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
	ASSERT_NE(reader, nullptr);
	std::string seen;
	for (size_t off = 0; off < doc.size() - hold_back; off += 4) {
		const size_t n = std::min<size_t>(4, doc.size() - hold_back - off);
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		ASSERT_EQ(gtext_yaml_reader_feed(reader, doc.data() + off, n, &err),
			GTEXT_YAML_OK);
		for (;;) {
			GTEXT_YAML_Event ev;
			memset(&ev, 0, sizeof(ev));
			memset(&err, 0, sizeof(err));
			if (gtext_yaml_reader_next(reader, &ev, &err) != GTEXT_YAML_OK) break;
			if (ev.type == GTEXT_YAML_EVENT_SCALAR) {
				seen += std::string(ev.data.scalar.ptr, ev.data.scalar.len);
				seen += "|";
			}
		}
	}
	EXPECT_NE(seen.find("one\ntwo\n"), std::string::npos)
		<< "nothing was emitted until the input ended; saw: " << seen;
	gtext_yaml_reader_free(reader);
}

/* The same question for a stream of documents, where a block scalar is ended
 * by the "..." that closes its document rather than by a dedent. This is the
 * shape streaming is actually for - a long series of documents down a pipe -
 * and without the marker check each one would wait for the end of the whole
 * stream. */
TEST(YamlChunkedFeed, ADocumentEndsEarlyEnoughToEmit) {
	const std::string doc =
		"--- |\nfirst\n...\n--- |\nsecond\n...\n--- |\nthird\n...\n";
	const size_t hold_back = 8;
	ASSERT_GT(doc.size(), hold_back);

	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
	ASSERT_NE(reader, nullptr);
	std::string seen;
	for (size_t off = 0; off < doc.size() - hold_back; off += 4) {
		const size_t n = std::min<size_t>(4, doc.size() - hold_back - off);
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		ASSERT_EQ(gtext_yaml_reader_feed(reader, doc.data() + off, n, &err),
			GTEXT_YAML_OK);
		for (;;) {
			GTEXT_YAML_Event ev;
			memset(&ev, 0, sizeof(ev));
			memset(&err, 0, sizeof(err));
			if (gtext_yaml_reader_next(reader, &ev, &err) != GTEXT_YAML_OK) break;
			if (ev.type == GTEXT_YAML_EVENT_SCALAR) {
				seen += std::string(ev.data.scalar.ptr, ev.data.scalar.len);
				seen += "|";
			}
		}
	}
	EXPECT_NE(seen.find("first\n"), std::string::npos)
		<< "the first document waited for the whole stream; saw: " << seen;
	gtext_yaml_reader_free(reader);
}

/* Checking for the whole block before taking any of it must not turn into a
 * rescan of the block on every feed.
 *
 * It did at first: a 0.9MB literal scalar delivered in 1KB pieces took 1.25s
 * where the same bytes in one piece took 0.03s, and the cost grows with the
 * square of the block's length - a caller reading from a socket in small
 * reads would pay it. The scan now resumes where it stopped.
 *
 * This is a timing test, which is worth saying out loud. It compares the two
 * feeds against each other rather than against a clock, so a loaded machine
 * slows both and the ratio holds: linear is a small multiple, and the shape
 * this guards against is roughly a thousand times the whole-input run at
 * these sizes. The bound is far enough from both to say which one it is. */
TEST(YamlChunkedFeed, ABigBlockScalarIsNotRescannedEveryFeed) {
	std::string doc = "big: |\n";
	doc.reserve(4u << 20);
	for (int i = 0; doc.size() < (4u << 20); i++) {
		doc += "  line ";
		doc += std::to_string(i);
		doc += " of a large literal block scalar\n";
	}

	auto feed = [&](size_t chunk) {
		GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
		if (!reader) return;
		size_t off = 0;
		while (off < doc.size()) {
			const size_t n = chunk ? std::min(chunk, doc.size() - off)
			                       : doc.size() - off;
			GTEXT_YAML_Error err;
			memset(&err, 0, sizeof(err));
			if (gtext_yaml_reader_feed(reader, doc.data() + off, n, &err)
					!= GTEXT_YAML_OK) {
				break;
			}
			for (;;) {
				GTEXT_YAML_Event ev;
				memset(&ev, 0, sizeof(ev));
				memset(&err, 0, sizeof(err));
				if (gtext_yaml_reader_next(reader, &ev, &err) != GTEXT_YAML_OK) break;
			}
			off += n;
		}
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));
		gtext_yaml_reader_feed(reader, nullptr, 0, &err);
		for (;;) {
			GTEXT_YAML_Event ev;
			memset(&ev, 0, sizeof(ev));
			memset(&err, 0, sizeof(err));
			if (gtext_yaml_reader_next(reader, &ev, &err) != GTEXT_YAML_OK) break;
		}
		gtext_yaml_reader_free(reader);
	};

	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	feed(0);
	const auto t1 = clock::now();
	feed(512);
	const auto t2 = clock::now();

	const double whole =
		std::chrono::duration<double>(t1 - t0).count();
	const double pieces =
		std::chrono::duration<double>(t2 - t1).count();
	/* Guard the denominator: a fast machine can read the whole document in
	 * less than the clock's resolution. */
	const double base = std::max(whole, 1e-4);
	EXPECT_LT(pieces / base, 50.0)
		<< "whole " << whole << "s, in 512-byte pieces " << pieces
		<< "s - the block looks like it is being rescanned on every feed";
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
