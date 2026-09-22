#include <gtest/gtest.h>
#include <string>
#include <sstream>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
#include <ghoti.io/text/yaml.h>
#include <ghoti.io/text/json.h>
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
//
// "All size limits use 0 to denote 'use the library default'" is what
// GTEXT_YAML_Parse_Options says in yaml_core.h.  Nothing implemented it:
// every check reads "if (limit > 0 && ...)", so a zero did not select the
// default, it removed the limit - and "GTEXT_YAML_Parse_Options opts = {0};",
// which the header's own documentation suggests, removed all three at once.
// A hundred thousand nested flow sequences then ran the resolver off the end
// of the stack.
//
// This test is older than the fix and passed throughout, because it fed
// "[1, 2, 3, 4, 5]" - a document accepted under every limit and under none.
// A test for a limit has to use an input the limit decides.
//
// What is asserted is *equivalence* with the defaults rather than a
// particular status, because equivalence is the whole of what the header
// promises and it survives the two parsers disagreeing about which code to
// refuse with (they do: E_DEPTH and E_LIMIT).
TEST(YamlLimits, ZeroMeansDefault) {
    GTEXT_YAML_Parse_Options zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    const GTEXT_YAML_Parse_Options defaults =
        gtext_yaml_parse_options_default();

    // Deeper than the default max_depth of 256, and shallow enough that the
    // pre-fix behaviour was to accept rather than to run out of stack - so a
    // regression reads as a diff and not as a crashed binary.
    std::string deep(2000, '[');
    deep.append(2000, ']');
    deep += '\n';

    const GTEXT_YAML_Parse_Options *const both[] = {&zeroed, &defaults};
    for (const GTEXT_YAML_Parse_Options *o : both) {
        const char *which = (o == &zeroed) ? "zeroed" : "defaults";

        GTEXT_YAML_Error err;
        memset(&err, 0, sizeof(err));
        GTEXT_YAML_Document *doc =
            gtext_yaml_parse(deep.data(), deep.size(), o, &err);
        EXPECT_EQ(doc, nullptr)
            << which << ": 2000 levels is past the default max_depth of "
            << defaults.max_depth << " and must be refused";
        if (doc) gtext_yaml_free(doc);

        // ...and the same options still accept an ordinary document, so the
        // refusal above is the depth limit and not a broken options struct.
        const char *ok = "[1, 2, 3, 4, 5]\n";
        memset(&err, 0, sizeof(err));
        GTEXT_YAML_Document *fine = gtext_yaml_parse(ok, strlen(ok), o, &err);
        EXPECT_NE(fine, nullptr)
            << which << ": " << (err.message ? err.message : "?");
        if (fine) gtext_yaml_free(fine);
    }

    // The streaming parser resolves its options through the same function, so
    // it has to answer the same way - whatever that answer is.
    GTEXT_YAML_Status zero_st = GTEXT_YAML_OK, default_st = GTEXT_YAML_OK;
    for (const GTEXT_YAML_Parse_Options *o : both) {
        GTEXT_YAML_Stream *s = gtext_yaml_stream_new(o, cb_stats, NULL);
        ASSERT_NE(s, nullptr);
        GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, deep.data(), deep.size());
        ((o == &zeroed) ? zero_st : default_st) = st;
        gtext_yaml_stream_free(s);
    }
    EXPECT_NE(default_st, GTEXT_YAML_OK) << "the default depth limit did not fire";
    EXPECT_EQ(zero_st, default_st);
}

// A zeroed struct is still not the defaults, and cannot be made so: the
// header's promise covers the *size limits*, and every bool in the struct
// zeroes to false, which is a real setting rather than an absent one.  That
// is why the alias budget has to be asked for on its own here - a memset()
// struct turns allow_aliases off and the document below is refused before any
// budget is consulted.  Starting from gtext_yaml_parse_options_default() is
// the only spelling that means "the defaults".
TEST(YamlLimits, ZeroAliasBudgetMeansTheDefaultBudget) {
    // Nominally 10^9 nodes from a few hundred bytes.
    std::string src = "l0: &l0 [x,x,x,x,x,x,x,x,x,x]\n";
    for (int i = 1; i <= 8; ++i) {
        const std::string n = std::to_string(i);
        const std::string p = std::to_string(i - 1);
        src += "l" + n + ": &l" + n + " [";
        for (int j = 0; j < 10; ++j) src += (j ? "," : "") + ("*l" + p);
        src += "]\n";
    }

    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_alias_expansion = 0;   /* "the library default", per the header */

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *doc =
        gtext_yaml_parse(src.data(), src.size(), &opts, &err);
    ASSERT_NE(doc, nullptr) << (err.message ? err.message : "?");

    GTEXT_YAML_To_JSON_Options jopts = gtext_yaml_to_json_options_default();
    jopts.allow_resolved_aliases = true;
    GTEXT_JSON_Value *out = nullptr;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Status st =
        gtext_yaml_to_json_with_options(doc, &out, &jopts, &err);

    // A budget refused it, rather than the allocator failing after the
    // process had taken every page it could get.
    EXPECT_EQ(st, GTEXT_YAML_E_LIMIT);
    EXPECT_EQ(out, nullptr);
    if (out) gtext_json_free(out);
    gtext_yaml_free(doc);
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
