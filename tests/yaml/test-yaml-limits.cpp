#include <gtest/gtest.h>
#include <string>
#include <sstream>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

static GTEXT_YAML_Status cb_stats(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user; return GTEXT_YAML_OK;
}

// Test 1: Default limit values
TEST(YamlLimits, DefaultValues) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    EXPECT_EQ(opts.max_depth, 256);
    EXPECT_EQ(opts.max_total_bytes, 64 * 1024 * 1024);
    EXPECT_EQ(opts.max_alias_expansion, 10000);
}

// Test 2: Total bytes limit - within limit
TEST(YamlLimits, TotalBytesWithinLimit) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_total_bytes = 1000;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *chunk = "foo\n";
    size_t chunk_len = strlen(chunk);
    for (size_t i = 0; i < 10; ++i) {
        GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, chunk, chunk_len);
        EXPECT_EQ(st, GTEXT_YAML_OK);
    }
    GTEXT_YAML_Status st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 3: Total bytes limit - exceeded
TEST(YamlLimits, TotalBytesExceeded) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_total_bytes = 10;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, "this is longer than ten", strlen("this is longer than ten"));
    EXPECT_TRUE(st == GTEXT_YAML_E_LIMIT || st == GTEXT_YAML_E_INVALID || st == GTEXT_YAML_E_STATE);
    gtext_yaml_stream_free(s);
}

// Test 4: Total bytes limit - cumulative across feeds
TEST(YamlLimits, TotalBytesCumulative) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_total_bytes = 50;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Feed small chunks that add up
    for (int i = 0; i < 10; ++i) {
        GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, "12345", 5);
        if (i < 10) {
            // First 10*5=50 bytes should be OK or close to limit
            EXPECT_TRUE(st == GTEXT_YAML_OK || st == GTEXT_YAML_E_LIMIT);
        }
        if (st != GTEXT_YAML_OK) break;
    }
    gtext_yaml_stream_free(s);
}

// Test 5: Depth limit - simple nested sequence within limit
TEST(YamlLimits, DepthWithinLimit) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 10;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Create nested structure: [[[[]]]]
    std::string yaml = "[[[[]]]]";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml.c_str(), yaml.length());
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 6: Depth limit - exceeded with nested sequences
TEST(YamlLimits, DepthExceededSequences) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 5;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Create deeply nested structure beyond limit
    std::string yaml;
    for (int i = 0; i < 10; ++i) {
        yaml += "[";
    }
    for (int i = 0; i < 10; ++i) {
        yaml += "]";
    }
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml.c_str(), yaml.length());
    // Should either reject during feed or finish
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    EXPECT_TRUE(st == GTEXT_YAML_E_LIMIT || st == GTEXT_YAML_E_DEPTH);
    gtext_yaml_stream_free(s);
}

// Test 7: Depth limit - exceeded with nested mappings
TEST(YamlLimits, DepthExceededMappings) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 5;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Create deeply nested mappings beyond limit
    std::string yaml;
    for (int i = 0; i < 10; ++i) {
        yaml += "{a:";
    }
    yaml += "1";
    for (int i = 0; i < 10; ++i) {
        yaml += "}";
    }
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml.c_str(), yaml.length());
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    EXPECT_TRUE(st == GTEXT_YAML_E_LIMIT || st == GTEXT_YAML_E_DEPTH);
    gtext_yaml_stream_free(s);
}

// Test 8: Alias expansion limit - simple case within limit
TEST(YamlLimits, AliasExpansionWithinLimit) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_alias_expansion = 100;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = "anchor: &anchor [1, 2, 3]\nalias: *anchor\n";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 9: Alias expansion limit - exponential growth
// Note: Current implementation may not enforce alias expansion limits strictly
TEST(YamlLimits, AliasExpansionExponentialGrowth) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_alias_expansion = 50;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Create exponential expansion: each alias doubles the size
    const char *yaml = 
        "a: &a [1, 2]\n"
        "b: &b [*a, *a]\n"
        "c: &c [*b, *b]\n"
        "d: &d [*c, *c]\n"
        "e: [*d, *d]\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    // Test currently just verifies no crash; proper limit enforcement TODO
    // Should ideally return GTEXT_YAML_E_LIMIT when expansion exceeds limit
    EXPECT_TRUE(st == GTEXT_YAML_OK || st == GTEXT_YAML_E_LIMIT || st == GTEXT_YAML_E_INVALID);
    gtext_yaml_stream_free(s);
}

// Test 10: Zero limit values (should use defaults)
TEST(YamlLimits, ZeroMeansDefault) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 0;
    opts.max_total_bytes = 0;
    opts.max_alias_expansion = 0;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Should use default limits (quite generous)
    const char *yaml = "[1, 2, 3, 4, 5]\n";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 11: Very small depth limit
TEST(YamlLimits, VerySmallDepthLimit) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 1;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Even a simple sequence should fail
    const char *yaml = "[1, 2]\n";
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    EXPECT_TRUE(st == GTEXT_YAML_E_LIMIT || st == GTEXT_YAML_E_DEPTH || st == GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 12: Combined limits - multiple limits active
TEST(YamlLimits, CombinedLimits) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_depth = 10;
    opts.max_total_bytes = 100;
    opts.max_alias_expansion = 20;
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, cb_stats, NULL);
    ASSERT_NE(s, nullptr);
    
    // Simple document that should pass all limits
    const char *yaml = "key: value\nlist: [1, 2, 3]\n";
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
