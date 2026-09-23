/**
 * yaml_context_line_start() against the walk it replaced.
 *
 * Five helpers in the parser needed "which line is this offset on" and each
 * had its own copy of a backwards walk to the nearest break. That is
 * O(line length) a call, and a deeply nested flow document is one line, so
 * the parse was quadratic in its depth.
 *
 * The answer is cached and extended forwards now, which is right for how the
 * parser asks - offsets that mostly move forwards - and wrong for nothing,
 * provided the cache agrees with the walk for *every* offset including the
 * ones that go backwards. A cache that is only checked in the order its
 * author expected is a cache that works until someone asks differently, so
 * this compares the two directly: every offset, forwards; every offset,
 * backwards; and a shuffled order that is neither.
 */
#include <gtest/gtest.h>
#include <stddef.h>
#include <string.h>

#include <string>
#include <vector>

extern "C" {
#include "../src/yaml/yaml_internal.h"
}

namespace {

/* The walk the five copies used to do, kept here as the reference. */
size_t NaiveLineStart(const std::string &buf, size_t offset) {
	if (offset > buf.size()) offset = buf.size();
	while (offset > 0) {
		const char ch = buf[offset - 1];
		if (ch == '\n' || ch == '\r') break;
		offset--;
	}
	return offset;
}

struct Ctx {
	yaml_context *c;
	explicit Ctx(const std::string &buf) : c(yaml_context_new(nullptr)) {
		if (c) yaml_context_set_decoded_input(c, buf.data(), buf.size());
	}
	~Ctx() { if (c) yaml_context_free(c); }
};

/* A deterministic shuffle, so a failure can be reproduced. */
std::vector<size_t> Shuffled(size_t n) {
	std::vector<size_t> v(n);
	for (size_t i = 0; i < n; i++) v[i] = i;
	unsigned long long st = 0x9e3779b97f4a7c15ULL;
	for (size_t i = n; i > 1; i--) {
		st ^= st << 13; st ^= st >> 7; st ^= st << 17;
		std::swap(v[i - 1], v[st % i]);
	}
	return v;
}

const std::string kDocs[] = {
	"",
	"a",
	"\n",
	"\r",
	"\r\n",
	"one line only, no break at all",
	"a\nb\nc\n",
	"a\r\nb\r\nc\r\n",
	"\n\n\n\n",
	"short\na much longer second line than the first one\nshort again\n",
	std::string(500, '[') + "x" + std::string(500, ']'),
	"lead\n" + std::string(300, '-') + "\ntail\n",
	std::string("mixed\r\nbreaks\rand\nkinds\r\n"),
};

}  // namespace

/* The control: the documents actually contain the shapes being claimed, so a
   pass means the comparison met line breaks rather than never finding one. */
TEST(YamlLineStartCache, TheFixturesCoverTheShapes) {
	bool has_lf = false, has_cr = false, has_crlf = false, has_long = false;
	for (const std::string &doc : kDocs) {
		if (doc.find('\n') != std::string::npos) has_lf = true;
		if (doc.find('\r') != std::string::npos) has_cr = true;
		if (doc.find("\r\n") != std::string::npos) has_crlf = true;
		if (doc.size() > 400) has_long = true;
	}
	EXPECT_TRUE(has_lf);
	EXPECT_TRUE(has_cr);
	EXPECT_TRUE(has_crlf);
	EXPECT_TRUE(has_long) << "no document long enough for the cache to matter";
}

TEST(YamlLineStartCache, ItAgreesWithTheWalkInEveryOrder) {
	for (const std::string &doc : kDocs) {
		const size_t n = doc.size() + 1;  /* offsets 0..size inclusive */

		{   /* forwards, which is how the parser asks */
			Ctx ctx(doc);
			ASSERT_NE(ctx.c, nullptr);
			for (size_t i = 0; i < n; i++) {
				EXPECT_EQ(yaml_context_line_start(ctx.c, i),
					NaiveLineStart(doc, i)) << "forward, offset " << i
					<< " of [" << doc << "]";
			}
		}
		{   /* backwards, which it does not, and which the cache must survive */
			Ctx ctx(doc);
			ASSERT_NE(ctx.c, nullptr);
			for (size_t i = n; i > 0; i--) {
				EXPECT_EQ(yaml_context_line_start(ctx.c, i - 1),
					NaiveLineStart(doc, i - 1)) << "backward, offset " << (i - 1)
					<< " of [" << doc << "]";
			}
		}
		{   /* and neither */
			Ctx ctx(doc);
			ASSERT_NE(ctx.c, nullptr);
			for (size_t i : Shuffled(n)) {
				EXPECT_EQ(yaml_context_line_start(ctx.c, i),
					NaiveLineStart(doc, i)) << "shuffled, offset " << i
					<< " of [" << doc << "]";
			}
		}
	}
}

/* Past the end is clamped, not read. */
TEST(YamlLineStartCache, AnOffsetPastTheEndIsClamped) {
	const std::string doc = "a\nbc";
	Ctx ctx(doc);
	ASSERT_NE(ctx.c, nullptr);
	EXPECT_EQ(yaml_context_line_start(ctx.c, doc.size() + 100),
		NaiveLineStart(doc, doc.size()));
}

/* The buffer moving is the case the cache must not survive. */
TEST(YamlLineStartCache, AMovedBufferDropsTheCache) {
	const std::string first = "aaaa\nbbbb";
	const std::string second = "x\ny\nzzzzzzzz";
	ASSERT_NE(first.data(), second.data());

	yaml_context *c = yaml_context_new(nullptr);
	ASSERT_NE(c, nullptr);

	yaml_context_set_decoded_input(c, first.data(), first.size());
	EXPECT_EQ(yaml_context_line_start(c, 8), NaiveLineStart(first, 8));

	yaml_context_set_decoded_input(c, second.data(), second.size());
	for (size_t i = 0; i <= second.size(); i++) {
		EXPECT_EQ(yaml_context_line_start(c, i), NaiveLineStart(second, i))
			<< "after the buffer moved, offset " << i;
	}
	yaml_context_free(c);
}
