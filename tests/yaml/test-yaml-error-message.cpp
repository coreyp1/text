/**
 * A refused document says which fault it hit.
 *
 * The scanner describes everything it rejects - "tab character used for
 * indentation", "Malformed block scalar header", "Unterminated verbatim tag"
 * - by filling in the GTEXT_YAML_Error it is handed. That error was a local
 * in the token loops in stream.c, which pass the status back up through a
 * plain `return st` and let the frame holding the message go away with it.
 * Every one of these arrived at the caller as the bare word "Parse error",
 * which names the category and nothing else: the same three words for a stray
 * tab on line 40 as for a quote nobody closed on line 2.
 *
 * The stream keeps the scanner's account now and the parse paths ask for it
 * before falling back. What this pins down is not the exact wording, which is
 * free to improve, but that the wording is *specific* - that two different
 * faults do not come back described identically, and that a position travels
 * with the message.
 */
#include <gtest/gtest.h>
#include <string>
#include <set>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <string.h>

/* The tests link the static archive, so the stream's own accessor is reachable
 * here even though the shared library does not export it. */
GTEXT_INTERNAL_API bool gtext_yaml_stream_last_error(
	const GTEXT_YAML_Stream *s, GTEXT_YAML_Error *out);
}

namespace {
GTEXT_YAML_Status NoopCallback(
		GTEXT_YAML_Stream *s, const void *payload, void *user) {
	(void)s; (void)payload; (void)user;
	return GTEXT_YAML_OK;
}
} // namespace

namespace {

struct Case {
	const char *name;
	const char *input;
};

/* Each of these is refused by the scanner, and each for its own reason. One
 * entry per reason: the two quote styles share "unterminated quoted scalar"
 * because they are the same fault, and that is worth keeping, not splitting.
 *
 * "a: b#c" is deliberately absent. A "#" with no white space in front of it
 * is ordinary plain content (ns-plain-char, 7.3.3), so that document is
 * sound and the mapping value is the string "b#c"; the separation rule only
 * has something to say where a comment could actually begin. */
const Case kScannerFaults[] = {
	{"unclosed quoted scalar", "a: \"unterminated\n"},
	{"tab where indentation belongs", "a:\n\tb: 1\n"},
	{"unclosed verbatim tag", "a: !<unterminated\n"},
	{"malformed block scalar header", "a: |9x\n  b\n"},
	{"comment with no space before it", "a: \"b\"#c\n"},
	{"flow line not indented past its node", "a: [1,\n2]\n"},
};

std::string MessageFor(const char *input) {
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	if (doc) {
		gtext_yaml_free(doc);
		return std::string();
	}
	return err.message ? std::string(err.message) : std::string("(none)");
}

} // namespace

TEST(YamlErrorMessage, ScannerFaultsAreNotAllCalledParseError) {
	for (const Case &c : kScannerFaults) {
		const std::string msg = MessageFor(c.input);
		EXPECT_FALSE(msg.empty())
			<< "should have been refused: " << c.name;
		EXPECT_NE(msg, "Parse error")
			<< "generic message for: " << c.name;
	}
}

TEST(YamlErrorMessage, DifferentFaultsReadDifferently) {
	std::set<std::string> seen;
	for (const Case &c : kScannerFaults) {
		const std::string msg = MessageFor(c.input);
		EXPECT_TRUE(seen.insert(msg).second)
			<< "shares its message with an earlier case: " << c.name
			<< " -> " << msg;
	}
}

/* The position matters as much as the text. A tab three lines down should not
 * be reported at the start of the document. */
TEST(YamlErrorMessage, CarriesThePositionOfTheFault) {
	const char *input = "a: 1\nb: 2\nc:\n\td: 3\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_EQ(doc, nullptr);
	ASSERT_NE(err.message, nullptr);
	EXPECT_STRNE(err.message, "Parse error");
	EXPECT_EQ(err.line, 4);
}

/* A document nothing is wrong with still parses, and leaves the error alone. */
TEST(YamlErrorMessage, SoundInputSetsNoError) {
	const char *input = "a: 1\nb: [2, 3]\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc =
		gtext_yaml_parse(input, strlen(input), nullptr, &err);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(err.code, GTEXT_YAML_OK);
	EXPECT_EQ(err.message, nullptr);
	gtext_yaml_free(doc);
}

/* The pull reader took the same status back from the same token loops and had
 * the same nothing to say about it. */
TEST(YamlErrorMessage, PullReaderReportsTheFaultToo) {
	const char *input = "a:\n\tb: 1\n";
	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
	ASSERT_NE(reader, nullptr);

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Status st =
		gtext_yaml_reader_feed(reader, input, strlen(input), &err);
	if (st == GTEXT_YAML_OK) {
		st = gtext_yaml_reader_feed(reader, nullptr, 0, &err);
	}
	EXPECT_NE(st, GTEXT_YAML_OK);
	ASSERT_NE(err.message, nullptr);
	EXPECT_STRNE(err.message, "Failed to parse YAML input");
	EXPECT_STRNE(err.message, "Failed to finalize YAML stream");

	gtext_yaml_reader_free(reader);
}

/* The stream reports nothing when nothing has gone wrong. A caller that asked
 * anyway used to be told "yes, an error" with a null message behind it, and
 * every path that consults this checks the message, so the lie was invisible -
 * which is exactly why it is worth pinning down here. */
TEST(YamlErrorMessage, NothingToReportOnSoundInput) {
	GTEXT_YAML_Stream *s = gtext_yaml_stream_new(nullptr, NoopCallback, nullptr);
	ASSERT_NE(s, nullptr);
	const char *input = "a: 1\nb: 2\n";
	ASSERT_EQ(gtext_yaml_stream_feed(s, input, strlen(input)), GTEXT_YAML_OK);
	ASSERT_EQ(gtext_yaml_stream_finish(s), GTEXT_YAML_OK);

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_FALSE(gtext_yaml_stream_last_error(s, &err));
	EXPECT_EQ(err.code, GTEXT_YAML_OK);
	gtext_yaml_stream_free(s);
}

/* Input that stops in the middle of a token is not a fault: more may still be
 * coming. Only the failure that arrives once the stream is finished counts. */
TEST(YamlErrorMessage, AHalfDeliveredTokenIsNotAFault) {
	GTEXT_YAML_Stream *s = gtext_yaml_stream_new(nullptr, NoopCallback, nullptr);
	ASSERT_NE(s, nullptr);
	const char *head = "a: \"still going";
	EXPECT_EQ(gtext_yaml_stream_feed(s, head, strlen(head)), GTEXT_YAML_OK);

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	EXPECT_FALSE(gtext_yaml_stream_last_error(s, &err))
		<< "an unfinished token was recorded as a failure";

	/* Finish it properly and the stream still has nothing to complain about. */
	const char *tail = "\"\n";
	EXPECT_EQ(gtext_yaml_stream_feed(s, tail, strlen(tail)), GTEXT_YAML_OK);
	EXPECT_EQ(gtext_yaml_stream_finish(s), GTEXT_YAML_OK);
	EXPECT_FALSE(gtext_yaml_stream_last_error(s, &err));
	gtext_yaml_stream_free(s);
}

/* And when the token never does arrive, the stream says so, with the message
 * and the status agreeing. */
TEST(YamlErrorMessage, StreamKeepsWhatTheScannerSaid) {
	GTEXT_YAML_Stream *s = gtext_yaml_stream_new(nullptr, NoopCallback, nullptr);
	ASSERT_NE(s, nullptr);
	const char *input = "a: \"never closed\n";
	GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
	if (st == GTEXT_YAML_OK) st = gtext_yaml_stream_finish(s);
	ASSERT_NE(st, GTEXT_YAML_OK);

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	ASSERT_TRUE(gtext_yaml_stream_last_error(s, &err));
	ASSERT_NE(err.message, nullptr);
	EXPECT_EQ(err.code, st);
	gtext_yaml_stream_free(s);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
