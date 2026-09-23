/**
 * Parsing a deeply nested document is linear in its depth.
 *
 * Five helpers in the parser need to know which line an offset is on, and
 * all five found out by walking backwards to the nearest line break. That is
 * O(line length) per call - and a deeply nested flow document is *one* line,
 * so every call re-covered the ground the last one had. Parsing 20000 nested
 * sequences walked 199,990,000 bytes backwards across 19999 calls.
 *
 * A second quadratic sat beside it: property_left_of_open_collection() walked
 * the whole parse stack for every event that had no properties, because
 * "none" was spelled two ways and it only knew one. That one is pinned by
 * test-yaml-event-properties.cpp, which asserts the cause rather than the
 * cost.
 *
 * This one has no cause to assert - the fix is that five call sites share a
 * cached lookup, and nothing observable says whether they do. So it measures
 * the shape instead, which is the thing anyone actually cares about.
 *
 * Measured over four sizes, best of three, before and after:
 *
 *     n        before    after
 *     12500     0.146s   0.005s
 *     25000     0.569s   0.008s
 *     50000     2.495s   0.015s
 *     100000   11.331s   0.030s
 *
 * The ratio per doubling is what this asserts, not any absolute time, because
 * a ratio survives a slow machine, a sanitizer build and a loaded box. A
 * quadratic parse doubles to 4x and a linear one to 2x, so the bar is set at
 * 3x - far enough from 2 that noise will not trip it and far enough from 4
 * that the defect cannot hide under it. Each size is timed three times and
 * the best taken: interference can only ever make a run slower, so the
 * minimum is the robust statistic here.
 */
#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

/* Seconds to parse n nested flow sequences, best of three. */
double BestParseSeconds(size_t n, size_t *depth_seen) {
	const std::string doc =
		std::string(n, '[') + "x" + std::string(n, ']');

	double best = 1e9;
	for (int run = 0; run < 3; run++) {
		GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
		opts.max_depth = SIZE_MAX;
		GTEXT_YAML_Error err;
		memset(&err, 0, sizeof(err));

		const auto t0 = std::chrono::steady_clock::now();
		GTEXT_YAML_Document *parsed =
			gtext_yaml_parse(doc.data(), doc.size(), &opts, &err);
		const auto t1 = std::chrono::steady_clock::now();

		EXPECT_NE(parsed, nullptr) << (err.message ? err.message : "?");
		gtext_yaml_error_free(&err);
		if (!parsed) return -1.0;

		if (depth_seen) {
			size_t d = 0;
			const GTEXT_YAML_Node *node = gtext_yaml_document_root(parsed);
			while (node && gtext_yaml_node_type(node) == GTEXT_YAML_SEQUENCE
					&& gtext_yaml_sequence_length(node) > 0) {
				node = gtext_yaml_sequence_get(node, 0);
				d++;
			}
			*depth_seen = d;
		}
		gtext_yaml_free(parsed);

		best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
	}
	return best;
}

}  // namespace

/* The control. If the document is not actually nesting, the timings below
   are measuring a flat parse and would be linear whatever the parser did. */
TEST(YamlParseScaling, TheFixtureReallyNests) {
	size_t depth = 0;
	ASSERT_GT(BestParseSeconds(2000, &depth), 0.0);
	EXPECT_EQ(depth, 2000u);
}

TEST(YamlParseScaling, DepthCostsLinearTime) {
	const size_t n = 100000;

	const double small = BestParseSeconds(n, nullptr);
	const double large = BestParseSeconds(n * 2, nullptr);
	ASSERT_GT(small, 0.0);
	ASSERT_GT(large, 0.0);

	/* A timer that cannot see the smaller run cannot measure a ratio out of
	   it either, so say so rather than dividing by something meaningless. */
	ASSERT_GT(small, 1e-4)
		<< "the " << n << "-deep parse was too fast to time; raise n";

	const double ratio = large / small;
	EXPECT_LT(ratio, 3.0)
		<< "doubling the depth multiplied the time by " << ratio
		<< " (" << small << "s -> " << large << "s). Linear is about 2, "
		<< "quadratic about 4.";
}
