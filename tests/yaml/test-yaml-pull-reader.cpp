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
