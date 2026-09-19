/**
 * @file test-yaml-pull-reader.cpp
 * @brief Tests for pull-model YAML reader.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
}

struct CapturedEvent {
	GTEXT_YAML_Event_Type type;
	std::string scalar;
};

static GTEXT_YAML_Status capture_push_cb(
	GTEXT_YAML_Stream *s,
	const void *payload,
	void *user
) {
	(void)s;
	std::vector<CapturedEvent> *events = (std::vector<CapturedEvent> *)user;
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)payload;
	if (!events || !event) return GTEXT_YAML_E_INVALID;

	if (event->type == GTEXT_YAML_EVENT_INDICATOR) {
		return GTEXT_YAML_OK;
	}

	CapturedEvent cap;
	cap.type = event->type;
	if (event->type == GTEXT_YAML_EVENT_SCALAR) {
		cap.scalar.assign(event->data.scalar.ptr, event->data.scalar.len);
	}
	events->push_back(cap);
	return GTEXT_YAML_OK;
}

static std::vector<CapturedEvent> capture_push_events(const char *yaml) {
	std::vector<CapturedEvent> events;
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_push_cb, &events);
	if (!stream) return events;

	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	gtext_yaml_stream_free(stream);
	return events;
}

static std::vector<CapturedEvent> capture_pull_events(
	const char *chunk1,
	const char *chunk2
) {
	std::vector<CapturedEvent> events;
	GTEXT_YAML_Error err = {};
	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(NULL);
	if (!reader) return events;

	GTEXT_YAML_Status status = gtext_yaml_reader_feed(
		reader,
		chunk1,
		strlen(chunk1),
		&err
	);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	bool fed_second = false;
	bool finished = false;
	for (;;) {
		GTEXT_YAML_Event event;
		status = gtext_yaml_reader_next(reader, &event, &err);
		if (status == GTEXT_YAML_OK) {
			if (event.type != GTEXT_YAML_EVENT_INDICATOR) {
				CapturedEvent cap;
				cap.type = event.type;
				if (event.type == GTEXT_YAML_EVENT_SCALAR) {
					cap.scalar.assign(event.data.scalar.ptr, event.data.scalar.len);
				}
				events.push_back(cap);
			}
			continue;
		}

		if (status == GTEXT_YAML_E_INCOMPLETE) {
			if (!fed_second) {
				status = gtext_yaml_reader_feed(
					reader,
					chunk2,
					strlen(chunk2),
					&err
				);
				EXPECT_EQ(status, GTEXT_YAML_OK);
				fed_second = true;
				continue;
			}
			if (!finished) {
				status = gtext_yaml_reader_feed(reader, NULL, 0, &err);
				EXPECT_EQ(status, GTEXT_YAML_OK);
				finished = true;
				continue;
			}
			break;
		}

		if (status == GTEXT_YAML_E_STATE) {
			break;
		}

		ADD_FAILURE() << "Unexpected status " << status;
		break;
	}

	gtext_yaml_reader_free(reader);
	return events;
}

TEST(YamlPullReader, EventSequenceMatchesPush) {
	const char *yaml = "[1, 2]";
	std::vector<CapturedEvent> push = capture_push_events(yaml);
	std::vector<CapturedEvent> pull = capture_pull_events("[1,", " 2]");

	std::vector<CapturedEvent> expected;
	CapturedEvent start;
	start.type = GTEXT_YAML_EVENT_STREAM_START;
	expected.push_back(start);
	for (const auto &item : push) {
		expected.push_back(item);
	}
	CapturedEvent end;
	end.type = GTEXT_YAML_EVENT_STREAM_END;
	expected.push_back(end);

	ASSERT_EQ(pull.size(), expected.size());
	for (size_t i = 0; i < expected.size(); i++) {
		EXPECT_EQ(pull[i].type, expected[i].type);
		EXPECT_EQ(pull[i].scalar, expected[i].scalar);
	}
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

// ---------------------------------------------------------------------------
// The event queue, past its initial capacity
//
// The pull reader buffers events in a ring until the caller drains them. The
// ring starts at eight entries and doubles, and growth copies the entries out
// in ring order into a fresh array. tools/coverage.sh reported that copy loop
// as never executed, which means no test had queued more than eight events
// before draining any.
//
// A ring copy that gets the modular arithmetic wrong does not crash - it
// reorders or duplicates events, and the reader goes on working. So the check
// is the full event sequence, compared against the same document drained
// event-by-event where the queue never grows at all.
// ---------------------------------------------------------------------------

namespace {

// Drain a document, either feeding it all first (which fills the queue and
// forces it to grow) or draining after every feed (which keeps it small).
std::vector<CapturedEvent> drain_document(
    const std::string &doc, bool drain_as_we_go) {
	std::vector<CapturedEvent> out;

	GTEXT_YAML_Reader *reader = gtext_yaml_reader_new(nullptr);
	EXPECT_NE(reader, nullptr);
	if (!reader) {
		return out;
	}

	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	auto pump = [&]() {
		for (;;) {
			GTEXT_YAML_Event ev;
			memset(&ev, 0, sizeof(ev));
			GTEXT_YAML_Status st = gtext_yaml_reader_next(reader, &ev, &err);
			if (st != GTEXT_YAML_OK) {
				break;
			}
			CapturedEvent c;
			c.type = ev.type;
			if (ev.type == GTEXT_YAML_EVENT_SCALAR && ev.data.scalar.ptr) {
				c.scalar.assign(ev.data.scalar.ptr, ev.data.scalar.len);
			}
			out.push_back(c);
		}
	};

	if (drain_as_we_go) {
		for (size_t i = 0; i < doc.size(); ++i) {
			EXPECT_EQ(gtext_yaml_reader_feed(reader, doc.data() + i, 1, &err),
			    GTEXT_YAML_OK);
			pump();
		}
	}
	else {
		EXPECT_EQ(
		    gtext_yaml_reader_feed(reader, doc.data(), doc.size(), &err),
		    GTEXT_YAML_OK);
	}

	// End of input.
	gtext_yaml_reader_feed(reader, nullptr, 0, &err);
	pump();

	gtext_yaml_reader_free(reader);
	gtext_yaml_error_free(&err);
	return out;
}

std::string sequence_document(int items) {
	std::string doc = "seq:\n";
	for (int i = 0; i < items; ++i) {
		doc += "  - item" + std::to_string(i) + "\n";
	}
	return doc;
}

} // namespace

TEST(YamlPullReader, QueueGrowthPreservesEventOrder) {
	// Well past the initial eight, so the ring grows several times with a
	// non-zero head.
	for (int items : {2, 7, 8, 9, 20, 64}) {
		SCOPED_TRACE("items=" + std::to_string(items));
		const std::string doc = sequence_document(items);

		const std::vector<CapturedEvent> buffered = drain_document(doc, false);
		const std::vector<CapturedEvent> incremental =
		    drain_document(doc, true);

		ASSERT_FALSE(buffered.empty());
		ASSERT_EQ(buffered.size(), incremental.size())
		    << "queueing everything first produced a different event count";

		for (size_t i = 0; i < buffered.size(); ++i) {
			EXPECT_EQ(buffered[i].type, incremental[i].type)
			    << "event " << i << " differs in type";
			EXPECT_EQ(buffered[i].scalar, incremental[i].scalar)
			    << "event " << i << " differs in value";
		}
	}
}

TEST(YamlPullReader, EveryScalarSurvivesQueueGrowth) {
	// The values themselves, in order, so a duplicated or dropped ring entry
	// is visible rather than only a count mismatch.
	const int items = 50;
	const std::vector<CapturedEvent> events =
	    drain_document(sequence_document(items), false);

	std::vector<std::string> scalars;
	for (const CapturedEvent &e : events) {
		if (e.type == GTEXT_YAML_EVENT_SCALAR) {
			scalars.push_back(e.scalar);
		}
	}

	// "seq" plus one per item.
	ASSERT_EQ(scalars.size(), static_cast<size_t>(items) + 1)
	    << "expected the key and " << items << " items";
	EXPECT_EQ(scalars[0], "seq");
	for (int i = 0; i < items; ++i) {
		EXPECT_EQ(scalars[static_cast<size_t>(i) + 1], "item" + std::to_string(i))
		    << "item " << i << " came back out of order";
	}
}
