/**
 * @file test-yaml-stream-debug.cpp
 * @brief Debug what events the streaming parser emits
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
}

struct CapturedEvent {
	GTEXT_YAML_Event_Type type;
	std::string scalar_value;
};

static std::vector<CapturedEvent> captured_events;

static GTEXT_YAML_Status capture_cb(GTEXT_YAML_Stream *s, const void *event_payload, void *user) {
	(void)s;
	(void)user;
	
	const GTEXT_YAML_Event *event = (const GTEXT_YAML_Event *)event_payload;
	
	CapturedEvent cap;
	cap.type = event->type;
	
	if (event->type == GTEXT_YAML_EVENT_SCALAR) {
		cap.scalar_value = std::string(event->data.scalar.ptr, event->data.scalar.len);
	}
	
	captured_events.push_back(cap);
	return GTEXT_YAML_OK;
}

static const char* event_type_name(GTEXT_YAML_Event_Type type) {
	switch (type) {
		case GTEXT_YAML_EVENT_STREAM_START: return "STREAM_START";
		case GTEXT_YAML_EVENT_STREAM_END: return "STREAM_END";
		case GTEXT_YAML_EVENT_DOCUMENT_START: return "DOCUMENT_START";
		case GTEXT_YAML_EVENT_DOCUMENT_END: return "DOCUMENT_END";
		case GTEXT_YAML_EVENT_SEQUENCE_START: return "SEQUENCE_START";
		case GTEXT_YAML_EVENT_SEQUENCE_END: return "SEQUENCE_END";
		case GTEXT_YAML_EVENT_MAPPING_START: return "MAPPING_START";
		case GTEXT_YAML_EVENT_MAPPING_END: return "MAPPING_END";
		case GTEXT_YAML_EVENT_SCALAR: return "SCALAR";
		case GTEXT_YAML_EVENT_ALIAS: return "ALIAS";
		case GTEXT_YAML_EVENT_INDICATOR: return "INDICATOR";
		default: return "UNKNOWN";
	}
}

TEST(YamlStreamDebug, FlowSequence) {
	captured_events.clear();
	
	const char *yaml = "[1, 2, 3]";
	
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, NULL);
	ASSERT_NE(stream, nullptr);
	
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	gtext_yaml_stream_free(stream);
	
	// Print captured events
	std::cout << "\nEvents for '[1, 2, 3]':\n";
	for (size_t i = 0; i < captured_events.size(); i++) {
		std::cout << "  " << i << ": " << event_type_name(captured_events[i].type);
		if (captured_events[i].type == GTEXT_YAML_EVENT_SCALAR) {
			std::cout << " '" << captured_events[i].scalar_value << "'";
		}
		std::cout << "\n";
	}
	
	// Check if we got sequence events
	bool has_seq_start = false;
	bool has_seq_end = false;
	for (const auto &ev : captured_events) {
		if (ev.type == GTEXT_YAML_EVENT_SEQUENCE_START) has_seq_start = true;
		if (ev.type == GTEXT_YAML_EVENT_SEQUENCE_END) has_seq_end = true;
	}
	
	std::cout << "Has SEQUENCE_START: " << (has_seq_start ? "YES" : "NO") << "\n";
	std::cout << "Has SEQUENCE_END: " << (has_seq_end ? "YES" : "NO") << "\n";
}

TEST(YamlStreamDebug, FlowMapping) {
	captured_events.clear();
	
	const char *yaml = "{key: value}";
	
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, NULL);
	ASSERT_NE(stream, nullptr);
	
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	gtext_yaml_stream_free(stream);
	
	// Print captured events
	std::cout << "\nEvents for '{key: value}':\n";
	for (size_t i = 0; i < captured_events.size(); i++) {
		std::cout << "  " << i << ": " << event_type_name(captured_events[i].type);
		if (captured_events[i].type == GTEXT_YAML_EVENT_SCALAR) {
			std::cout << " '" << captured_events[i].scalar_value << "'";
		}
		std::cout << "\n";
	}
	
	// Check if we got mapping events
	bool has_map_start = false;
	bool has_map_end = false;
	for (const auto &ev : captured_events) {
		if (ev.type == GTEXT_YAML_EVENT_MAPPING_START) has_map_start = true;
		if (ev.type == GTEXT_YAML_EVENT_MAPPING_END) has_map_end = true;
	}
	
	std::cout << "Has MAPPING_START: " << (has_map_start ? "YES" : "NO") << "\n";
	std::cout << "Has MAPPING_END: " << (has_map_end ? "YES" : "NO") << "\n";
}

TEST(YamlStreamDebug, BlockMapping) {
	captured_events.clear();
	
	const char *yaml = "key: value";
	
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, NULL);
	ASSERT_NE(stream, nullptr);
	
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	gtext_yaml_stream_free(stream);
	
	// Print captured events
	std::cout << "\nEvents for 'key: value':\n";
	for (size_t i = 0; i < captured_events.size(); i++) {
		std::cout << "  " << i << ": " << event_type_name(captured_events[i].type);
		if (captured_events[i].type == GTEXT_YAML_EVENT_SCALAR) {
			std::cout << " '" << captured_events[i].scalar_value << "'";
		}
		std::cout << "\n";
	}
	
	// Check if we got mapping events
	bool has_map_start = false;
	bool has_map_end = false;
	for (const auto &ev : captured_events) {
		if (ev.type == GTEXT_YAML_EVENT_MAPPING_START) has_map_start = true;
		if (ev.type == GTEXT_YAML_EVENT_MAPPING_END) has_map_end = true;
	}
	
	std::cout << "Has MAPPING_START: " << (has_map_start ? "YES" : "NO") << "\n";
	std::cout << "Has MAPPING_END: " << (has_map_end ? "YES" : "NO") << "\n";
}

TEST(YamlStreamDebug, BlockSequence) {
	captured_events.clear();
	
	const char *yaml = "- one\n- two\n- three";
	
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, NULL);
	ASSERT_NE(stream, nullptr);
	
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	gtext_yaml_stream_free(stream);
	
	// Print captured events
	std::cout << "\nEvents for '- one\\n- two\\n- three':\n";
	for (size_t i = 0; i < captured_events.size(); i++) {
		std::cout << "  " << i << ": " << event_type_name(captured_events[i].type);
		if (captured_events[i].type == GTEXT_YAML_EVENT_SCALAR) {
			std::cout << " '" << captured_events[i].scalar_value << "'";
		}
		std::cout << "\n";
	}
	
	// Check if we got sequence events
	bool has_seq_start = false;
	bool has_seq_end = false;
	for (const auto &ev : captured_events) {
		if (ev.type == GTEXT_YAML_EVENT_SEQUENCE_START) has_seq_start = true;
		if (ev.type == GTEXT_YAML_EVENT_SEQUENCE_END) has_seq_end = true;
	}
	
	std::cout << "Has SEQUENCE_START: " << (has_seq_start ? "YES" : "NO") << "\n";
	std::cout << "Has SEQUENCE_END: " << (has_seq_end ? "YES" : "NO") << "\n";
}

TEST(YamlStreamDebug, BareScalar) {
	captured_events.clear();
	
	const char *yaml = "hello";
	
	GTEXT_YAML_Stream *stream = gtext_yaml_stream_new(NULL, capture_cb, NULL);
	ASSERT_NE(stream, nullptr);
	
	GTEXT_YAML_Status status = gtext_yaml_stream_feed(stream, yaml, strlen(yaml));
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	status = gtext_yaml_stream_finish(stream);
	EXPECT_EQ(status, GTEXT_YAML_OK);
	
	gtext_yaml_stream_free(stream);
	
	// Print captured events
	std::cout << "\nEvents for 'hello':\n";
	for (size_t i = 0; i < captured_events.size(); i++) {
		std::cout << "  " << i << ": " << event_type_name(captured_events[i].type);
		if (captured_events[i].type == GTEXT_YAML_EVENT_SCALAR) {
			std::cout << " '" << captured_events[i].scalar_value << "'";
		}
		std::cout << "\n";
	}
	
	//Check for DOCUMENT_END
	bool has_doc_end = false;
	for (const auto &ev : captured_events) {
		if (ev.type == GTEXT_YAML_EVENT_DOCUMENT_END) has_doc_end = true;
	}
	std::cout << "Has DOCUMENT_END: " << (has_doc_end ? "YES" : "NO") << "\n";
}
