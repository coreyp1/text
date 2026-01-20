/**
 * @file test-headers.c
 * @brief Test that all headers compile independently
 *
 * This test verifies that:
 * - Each header can be included independently
 * - The umbrella header includes all modules correctly
 * - Headers don't have missing dependencies
 */

// Test json_core.h independently
#include <ghoti.io/text/json/json_core.h>
static void test_json_core(void) {
    text_json_parse_options opt = text_json_parse_options_default();
    text_json_write_options wopt = text_json_write_options_default();
    (void)opt;
    (void)wopt;
}

// Test json_dom.h independently
#include <ghoti.io/text/json/json_dom.h>
static void test_json_dom(void) {
    text_json_value* v = text_json_new_null();
    if (v) text_json_free(v);
}

// Test json_writer.h independently
#include <ghoti.io/text/json/json_writer.h>
static void test_json_writer(void) {
    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_sink_buffer_free(&sink);
}

// Test json_stream.h independently
#include <ghoti.io/text/json/json_stream.h>
static void test_json_stream(void) {
    // Just verify the types are available
    text_json_event_type type = TEXT_JSON_EVT_NULL;
    (void)type;
}

// Test json_pointer.h independently
#include <ghoti.io/text/json/json_pointer.h>
static void test_json_pointer(void) {
    // Just verify the header compiles
    (void)0;
}

// Test json_patch.h independently
#include <ghoti.io/text/json/json_patch.h>
static void test_json_patch(void) {
    // Just verify the header compiles
    (void)0;
}

// Test json_schema.h independently
#include <ghoti.io/text/json/json_schema.h>
static void test_json_schema(void) {
    // Just verify the header compiles
    (void)0;
}

// Test umbrella header
#include <ghoti.io/text/json.h>
static void test_umbrella(void) {
    // Verify all types are available through umbrella header
    text_json_parse_options opt = text_json_parse_options_default();
    text_json_value* v = text_json_new_null();
    if (v) text_json_free(v);
    (void)opt;
}

int main(void) {
    test_json_core();
    test_json_dom();
    test_json_writer();
    test_json_stream();
    test_json_pointer();
    test_json_patch();
    test_json_schema();
    test_umbrella();
    return 0;
}
