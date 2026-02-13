/**
 * @file test-yaml-nested-structures.cpp
 * @brief Comprehensive tests for nested YAML structures (sequences, mappings, mixed)
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
#include <stdlib.h>
#include <string.h>
}

//
// Helper: Count events by type
//
struct EventCounts {
  int scalars;
  int indicators;
  int structures;  // SEQUENCE/MAPPING START/END events
  int total;
};

static EventCounts counts = {0, 0, 0, 0};

static GTEXT_YAML_Status counting_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
  (void)s; (void)user;
  const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)evp;
  counts.total++;
  if (e->type == GTEXT_YAML_EVENT_SCALAR) counts.scalars++;
  if (e->type == GTEXT_YAML_EVENT_INDICATOR) counts.indicators++;
  if (e->type == GTEXT_YAML_EVENT_SEQUENCE_START || e->type == GTEXT_YAML_EVENT_SEQUENCE_END ||
      e->type == GTEXT_YAML_EVENT_MAPPING_START || e->type == GTEXT_YAML_EVENT_MAPPING_END) {
    counts.structures++;
  }
  return GTEXT_YAML_OK;
}

static void reset_counts() {
  counts.scalars = 0;
  counts.indicators = 0;
  counts.structures = 0;
  counts.total = 0;
}

//
// Test: Simple nested sequence [[1,2],[3,4]]
//
TEST(YamlNested, SimpleNestedSequence) {
  const char *input = "[[1,2],[3,4]]";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should have: [ [ 1 , 2 ] , [ 3 , 4 ] ]
  // Structures: 4 SEQUENCE_START + 4 SEQUENCE_END = 8
  // Indicators: 3 commas
  // Scalars: 1 2 3 4 = 4
  EXPECT_EQ(counts.scalars, 4);
  EXPECT_GE(counts.structures + counts.indicators, 8);
}

//
// Test: Nested mappings {a:{b:{c:1}}}
//
TEST(YamlNested, NestedMappings) {
  const char *input = "{a:{b:{c:1}}}";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should have: { a : { b : { c : 1 } } }
  // Structures: 3 MAPPING_START + 3 MAPPING_END = 6
  // Indicators: 3 colons
  // Scalars: a b c 1 = 4
  EXPECT_EQ(counts.scalars, 4);
  EXPECT_GE(counts.structures + counts.indicators, 9);
}

//
// Test: Mixed nesting {a:[1,{b:2}]}
//
TEST(YamlNested, MixedNesting) {
  const char *input = "{a:[1,{b:2}]}";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should have: { a : [ 1 , { b : 2 } ] }
  // Scalars: a 1 b 2 = 4
  EXPECT_EQ(counts.scalars, 4);
  EXPECT_GE(counts.total, 8);
}

//
// Test: Depth level 5 nesting
//
TEST(YamlNested, DepthLevel5) {
  const char *input = "[[[[[hello]]]]]";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should have 1 scalar "hello" and 10 brackets (5 START + 5 END = 10 structure events)
  EXPECT_EQ(counts.scalars, 1);
  EXPECT_GE(counts.structures, 10);
}

//
// Test: Depth level 10 nesting within limits
//
TEST(YamlNested, DepthLevel10WithinLimits) {
  const char *input = "[[[[[[[[[[x]]]]]]]]]]";
  
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_depth = 12;  // Allow depth 10
  
  reset_counts();
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should succeed with 1 scalar and 20 brackets (10 START + 10 END = 20 structure events)
  EXPECT_EQ(counts.scalars, 1);
  EXPECT_GE(counts.structures, 20);
}

//
// Test: Depth level exceeds limit
//
TEST(YamlNested, DepthExceedsLimit) {
  const char *input = "[[[[[x]]]]]";  // Depth 5
  
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_depth = 3;  // Only allow depth 3
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  
  // Should fail with depth exceeded
  EXPECT_EQ(st, GTEXT_YAML_E_DEPTH);
  
  gtext_yaml_stream_free(s);
}

//
// Test: Empty nested structures
//
TEST(YamlNested, EmptyNestedStructures) {
  const char *input = "[[],[[]],{}]";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Empty structures, no scalars
  // [[],[[]],{}] has: outer [], first [], nested [[]], and {}
  // Structures: 2+2+4+2 = 10 structure events, plus 2 commas
  EXPECT_EQ(counts.scalars, 0);
  EXPECT_GE(counts.structures + counts.indicators, 10);
}

//
// Test: Complex real-world-like structure
//
TEST(YamlNested, ComplexStructure) {
  const char *input = "{users:[{name:alice,age:30},{name:bob,age:25}],count:2}";
  reset_counts();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, counting_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Scalars: users, name, alice, age, 30, name, bob, age, 25, count, 2 = 11
  EXPECT_EQ(counts.scalars, 11);
  EXPECT_GE(counts.total, 20);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
