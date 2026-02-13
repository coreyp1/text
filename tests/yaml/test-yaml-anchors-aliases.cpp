#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

static GTEXT_YAML_Status noop_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user; 
    return GTEXT_YAML_OK;
}

// Test 1: Simple anchor and alias
TEST(YamlAnchorsAliases, SimpleAnchorAlias) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = "anchor: &anchor value\nalias: *anchor\n";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 2: Multiple anchors with different aliases
TEST(YamlAnchorsAliases, MultipleAnchors) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "a1: &anchor1 value1\n"
        "a2: &anchor2 value2\n"
        "a3: &anchor3 value3\n"
        "b1: *anchor1\n"
        "b2: *anchor2\n"
        "b3: *anchor3\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 3: Anchor with sequence value
TEST(YamlAnchorsAliases, AnchorSequence) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "list: &mylist [1, 2, 3, 4]\n"
        "copy: *mylist\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 4: Anchor with mapping value
TEST(YamlAnchorsAliases, AnchorMapping) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "defaults: &defaults\n"
        "  adapter: postgres\n"
        "  host: localhost\n"
        "development:\n"
        "  <<: *defaults\n"
        "  database: dev_db\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 5: Nested anchors and aliases
TEST(YamlAnchorsAliases, NestedAnchors) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "outer: &outer\n"
        "  inner: &inner value\n"
        "  another: something\n"
        "copy_outer: *outer\n"
        "copy_inner: *inner\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 6: Alias used multiple times
TEST(YamlAnchorsAliases, ReusedAlias) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "anchor: &reused [a, b, c]\n"
        "first: *reused\n"
        "second: *reused\n"
        "third: *reused\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 7: Undefined alias (should be handled gracefully)
TEST(YamlAnchorsAliases, UndefinedAlias) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = "key: *undefined\n";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    // Parser may be lenient and treat as plain scalar or reject
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    EXPECT_TRUE(st == GTEXT_YAML_OK || st == GTEXT_YAML_E_INVALID);
    gtext_yaml_stream_free(s);
}

// Test 8: Anchor defined after alias (forward reference - invalid)
TEST(YamlAnchorsAliases, ForwardReference) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "alias: *forward\n"
        "anchor: &forward value\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    // Should either reject or treat *forward as plain scalar before anchor
    EXPECT_TRUE(st == GTEXT_YAML_OK || st == GTEXT_YAML_E_INVALID);
    gtext_yaml_stream_free(s);
}

// Test 9: Anchor name with special characters
TEST(YamlAnchorsAliases, AnchorSpecialChars) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    // Anchor names can contain alphanumerics, -, and _
    const char *yaml = 
        "item: &my-anchor_123 value\n"
        "copy: *my-anchor_123\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 10: Anchor in flow sequence
TEST(YamlAnchorsAliases, AnchorInFlowSequence) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "list: [&a 1, &b 2, &c 3]\n"
        "values: [*a, *b, *c]\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 11: Anchor in flow mapping
TEST(YamlAnchorsAliases, AnchorInFlowMapping) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "map: {key1: &v1 val1, key2: &v2 val2}\n"
        "copy: {a: *v1, b: *v2}\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 12: Deeply nested alias references
TEST(YamlAnchorsAliases, DeeplyNestedAliases) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "level1: &l1\n"
        "  level2: &l2\n"
        "    level3: &l3\n"
        "      value: deep\n"
        "ref1: *l1\n"
        "ref2: *l2\n"
        "ref3: *l3\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 13: Chain of aliases (alias referring to another alias)
TEST(YamlAnchorsAliases, ChainedAliases) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "original: &orig value\n"
        "first: &first *orig\n"
        "second: *first\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 14: Anchor on empty sequence
TEST(YamlAnchorsAliases, EmptySequenceAnchor) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "empty: &empty []\n"
        "copy: *empty\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 15: Anchor on empty mapping
TEST(YamlAnchorsAliases, EmptyMappingAnchor) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "empty: &empty {}\n"
        "copy: *empty\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 16: Mixed anchors and aliases in complex document
TEST(YamlAnchorsAliases, ComplexDocument) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "defaults: &defaults\n"
        "  timeout: 30\n"
        "  retries: 3\n"
        "config1:\n"
        "  <<: *defaults\n"
        "  name: service1\n"
        "config2:\n"
        "  <<: *defaults\n"
        "  name: service2\n"
        "  timeout: 60\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 17: Alias within alias (nested structure containing aliases)
TEST(YamlAnchorsAliases, AliasContainingAliases) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "inner: &inner value\n"
        "outer: &outer [*inner, *inner]\n"
        "copy: *outer\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 18: Anchor reused within same collection
TEST(YamlAnchorsAliases, AnchorReusedInCollection) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "item: &item value\n"
        "list: [*item, *item, *item, *item]\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
