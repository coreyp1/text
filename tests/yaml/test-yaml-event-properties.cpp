/**
 * "No properties here" has one spelling, and it is not zero.
 *
 * GTEXT_YAML_Event carries prop_line and prop_col: where the anchor or tag
 * that names this node was written. Absent, they are -1, because line 0 and
 * column 0 are real places a property could be. Every event was being cleared
 * with memset and six of the seven construction sites left both at 0, which
 * reads as "a property, at the very start of the document".
 *
 * It never produced a wrong answer, which is why it lasted. The consumer that
 * could have been fooled - property_left_of_open_collection() - walks the
 * parse stack comparing each level's source line >= the property's, and every
 * real line is >= 0, so it always fell through to "no". What it cost was
 * time: that walk ran for every property-less event, which is O(depth) per
 * event and quadratic over a deeply nested document. Parsing 20000 nested
 * flow sequences took 200,010,000 stack steps, every one of them to compute
 * false. One reader had already noticed and defended itself locally with an
 * extra `prop_line > 0`; the other had not.
 *
 * This is the property test rather than a timing test, because the sentinel
 * is the cause and the time is only a symptom. A new event type that forgets
 * it brings the quadratic back.
 */
#include <gtest/gtest.h>
#include <string.h>

#include <string>
#include <vector>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

namespace {

struct Seen {
	GTEXT_YAML_Event_Type type;
	int prop_line;
	int prop_col;
};

GTEXT_YAML_Status collect(
		GTEXT_YAML_Stream *s, const void *payload, void *user) {
	(void)s;
	const GTEXT_YAML_Event *ev = (const GTEXT_YAML_Event *)payload;
	static_cast<std::vector<Seen> *>(user)->push_back(
		Seen{ev->type, ev->prop_line, ev->prop_col});
	return GTEXT_YAML_OK;
}

std::vector<Seen> EventsOf(const std::string &src) {
	std::vector<Seen> out;
	GTEXT_YAML_Stream *s = gtext_yaml_stream_new(nullptr, collect, &out);
	EXPECT_NE(s, nullptr);
	if (!s) return out;
	EXPECT_EQ(gtext_yaml_stream_feed(s, src.data(), src.size()), GTEXT_YAML_OK)
		<< src;
	EXPECT_EQ(gtext_yaml_stream_finish(s), GTEXT_YAML_OK) << src;
	gtext_yaml_stream_free(s);
	return out;
}

}  // namespace

/* The control. If no events arrive, every assertion below holds vacuously. */
TEST(YamlEventProperties, TheEventsArriveAtAll) {
	const std::vector<Seen> events = EventsOf("[a, [b, c], {d: e}]\n");
	EXPECT_GT(events.size(), 5u);
}

/* A document with no anchor and no tag anywhere has no properties anywhere,
   so every event it produces must say so. */
TEST(YamlEventProperties, ADocumentWithoutPropertiesReportsNoneOnEveryEvent) {
	const char *sources[] = {
		"[a, [b, c], {d: e}]\n",
		"a: 1\nb:\n  - x\n  - y\n",
		"[[[[[x]]]]]\n",
		"? k\n: v\n",
		"--- a\n--- b\n",
		"k: |\n  literal\n",
		"- &keep 1\n",  /* one anchor: the rest of the events still have none */
	};
	for (const char *src : sources) {
		const std::vector<Seen> events = EventsOf(src);
		ASSERT_FALSE(events.empty()) << src;
		for (const Seen &e : events) {
			/* Either a real property, on a real line, or the absent sentinel.
			   What must never appear is the half-state: a column with no
			   line, or line 0 standing in for "none". */
			if (e.prop_col < 0) {
				EXPECT_LT(e.prop_line, 0)
					<< "no column but line " << e.prop_line
					<< " (event type " << (int)e.type << ") for: " << src;
			}
			else {
				EXPECT_GT(e.prop_line, 0)
					<< "property at column " << e.prop_col << " on line "
					<< e.prop_line << " (event type " << (int)e.type
					<< ") for: " << src;
			}
		}
	}
}

/* And the closing events specifically, which are the ones that were wrong:
   a "]" or "}" names nothing, so it can never carry a property. */
TEST(YamlEventProperties, AClosingEventNeverCarriesAProperty) {
	const std::vector<Seen> events = EventsOf("&a [1, &b {c: d}]\n");
	size_t closings = 0;
	for (const Seen &e : events) {
		if (e.type != GTEXT_YAML_EVENT_SEQUENCE_END
				&& e.type != GTEXT_YAML_EVENT_MAPPING_END) {
			continue;
		}
		closings++;
		EXPECT_LT(e.prop_col, 0) << "a closing event claimed a property at "
			<< "column " << e.prop_col;
		EXPECT_LT(e.prop_line, 0);
	}
	/* The control: the document really does close two collections, so the
	   loop above examined something. */
	EXPECT_EQ(closings, 2u);
}

/* The other half of the sentinel: a property that really is there is still
   reported, so the fix did not simply switch the feature off. */
TEST(YamlEventProperties, ARealPropertyIsStillReported) {
	const std::vector<Seen> events = EventsOf("&anchor\n- 1\n");
	size_t with_props = 0;
	for (const Seen &e : events) {
		if (e.prop_col >= 0) {
			with_props++;
			EXPECT_GT(e.prop_line, 0);
		}
	}
	EXPECT_GT(with_props, 0u)
		<< "an anchor on its own line reported no property on any event";
}
