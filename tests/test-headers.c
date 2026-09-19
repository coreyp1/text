/**
 * @file test-headers.c
 * @brief Runtime smoke test for the types and functions each header declares
 *
 * Whether a header compiles on its own is checked by `make check-headers`,
 * which globs include/ and so cannot fall behind the tree.  This file does
 * the part a glob cannot: it constructs the types and calls the entry points,
 * so that a header which compiles but declares something the library does not
 * define fails here rather than in a consumer's link.
 *
 * Every function defined here must be called from main().  The YAML and CSV
 * smoke tests below went uncalled for some time, which the build did not
 * report because -Wno-error=unused-function is on for other reasons.
 */

// Test json_core.h independently
#include <ghoti.io/text/json/json_core.h>
static void test_json_core(void) {
  GTEXT_JSON_Parse_Options opt = gtext_json_parse_options_default();
  GTEXT_JSON_Write_Options wopt = gtext_json_write_options_default();
  (void)opt;
  (void)wopt;
}

// Test json_dom.h independently
#include <ghoti.io/text/json/json_dom.h>
static void test_json_dom(void) {
  GTEXT_JSON_Value * v = gtext_json_new_null();
  if (v)
    gtext_json_free(v);
}

// Test json_writer.h independently
#include <ghoti.io/text/json/json_writer.h>
static void test_json_writer(void) {
  GTEXT_JSON_Sink sink;
  gtext_json_sink_buffer(&sink);
  gtext_json_sink_buffer_free(&sink);
}

// Test json_stream.h independently
#include <ghoti.io/text/json/json_stream.h>
static void test_json_stream(void) {
  // Just verify the types are available
  GTEXT_JSON_Event_Type type = GTEXT_JSON_EVT_NULL;
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
  GTEXT_JSON_Parse_Options opt = gtext_json_parse_options_default();
  GTEXT_JSON_Value * v = gtext_json_new_null();
  if (v)
    gtext_json_free(v);
  (void)opt;
}

// --- YAML header smoke tests (compile-only) ---
#include <ghoti.io/text/yaml/yaml_core.h>
static void test_yaml_core(void) {
  GTEXT_YAML_Parse_Options opt = gtext_yaml_parse_options_default();
  GTEXT_YAML_Write_Options wopt = gtext_yaml_write_options_default();
  (void)opt;
  (void)wopt;
}

#include <ghoti.io/text/yaml/yaml_dom.h>
static void test_yaml_dom(void) {
  const char * s = "";
  (void)s;
}

#include <ghoti.io/text/yaml/yaml_writer.h>
static void test_yaml_writer(void) {
  /* Verify writer symbol available */
  (void)gtext_yaml_write_options_default;
}

#include <ghoti.io/text/yaml/yaml_stream.h>
static void test_yaml_stream(void) {
  /* Verify stream types */
  (void)GTEXT_YAML_OK;
}

// umbrella
#include <ghoti.io/text/yaml.h>
static void test_yaml_umbrella(void) {
  GTEXT_YAML_Parse_Options opt = gtext_yaml_parse_options_default();
  (void)opt;
}

// --- CSV header smoke tests ---
#include <ghoti.io/text/csv/csv_core.h>
static void test_csv_core(void) {
  GTEXT_CSV_Parse_Options opt = gtext_csv_parse_options_default();
  GTEXT_CSV_Write_Options wopt = gtext_csv_write_options_default();
  (void)opt;
  (void)wopt;
}

#include <ghoti.io/text/csv/csv_table.h>
static void test_csv_table(void) {
  (void)GTEXT_CSV_OK;
}

#include <ghoti.io/text/csv/csv_stream.h>
static void test_csv_stream(void) {
  (void)gtext_csv_parse_options_default;
}

#include <ghoti.io/text/csv/csv_writer.h>
static void test_csv_writer(void) {
  (void)gtext_csv_write_options_default;
}

// umbrella
#include <ghoti.io/text/csv.h>
static void test_csv_umbrella(void) {
  GTEXT_CSV_Parse_Options opt = gtext_csv_parse_options_default();
  (void)opt;
}

// --- Library-wide headers ---
#include <ghoti.io/text/allocator.h>
static void test_allocator(void) {
  const GTEXT_Allocator * a = gtext_allocator_default();
  (void)a;
}

#include <ghoti.io/text/text.h>
static void test_text(void) {
  (void)gtext_version_string();
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

  test_yaml_core();
  test_yaml_dom();
  test_yaml_writer();
  test_yaml_stream();
  test_yaml_umbrella();

  test_csv_core();
  test_csv_table();
  test_csv_stream();
  test_csv_writer();
  test_csv_umbrella();

  test_allocator();
  test_text();
  return 0;
}
