#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_core.h>
#include <stdio.h> // for snprintf

// Forward declarations for internal resolver API
typedef struct ResolverState ResolverState;
ResolverState *gtext_yaml_resolver_new(const GTEXT_YAML_Parse_Options *opts);
void gtext_yaml_resolver_free(ResolverState *r);
int gtext_yaml_resolver_register_anchor(ResolverState *r, const char *name, size_t size);
int gtext_yaml_resolver_register_anchor_with_refs(ResolverState *r, const char *name, size_t base_size, const char **refs, size_t ref_count);
}

/**
 * Test for Task 9.2: Resolver memory leak fixes
 * This test verifies that the resolver properly frees all allocated memory
 * including anchor_defs when freed.
 */

TEST(YamlResolverCleanup, CreateAndDestroy) {
  // Test 9.2.4: Create and destroy resolver multiple times
  for (int i = 0; i < 10; ++i) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.max_alias_expansion = 1000;
    
    ResolverState *r = gtext_yaml_resolver_new(&opts);
    ASSERT_NE(r, nullptr);
    
    // Register some anchors
    EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, "anchor1", 5), 1);
    EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, "anchor2", 10), 1);
    EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, "anchor3", 3), 1);
    
    // Free should clean up everything (including anchor_defs)
    gtext_yaml_resolver_free(r);
  }
  // Run under valgrind - should show zero leaks
}

TEST(YamlResolverCleanup, ComplexAnchorsWithRefs) {
  // Test 9.2.5: Complex anchor graph to stress-test cleanup
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_alias_expansion = 10000;
  
  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);
  
  // Create a complex graph with multiple references
  const char *refs1[] = {"b", "c"};
  const char *refs2[] = {"c", "d"};
  const char *refs3[] = {"a"};
  
  // Register anchors with references (anchor_defs list)
  EXPECT_EQ(gtext_yaml_resolver_register_anchor_with_refs(r, "a", 1, refs1, 2), 1);
  EXPECT_EQ(gtext_yaml_resolver_register_anchor_with_refs(r, "b", 2, refs2, 2), 1);
  EXPECT_EQ(gtext_yaml_resolver_register_anchor_with_refs(r, "c", 1, refs3, 1), 1);
  EXPECT_EQ(gtext_yaml_resolver_register_anchor_with_refs(r, "d", 1, nullptr, 0), 1);
  
  // Also register simple anchors (anchors list)
  EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, "simple1", 5), 1);
  EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, "simple2", 10), 1);
  
  // Free should clean up both anchor lists and all strings
  gtext_yaml_resolver_free(r);
  // Run under valgrind - should show zero leaks
}

TEST(YamlResolverCleanup, NullResolver) {
  // Edge case: freeing null should be safe
  gtext_yaml_resolver_free(nullptr);
  SUCCEED(); // Should not crash
}

TEST(YamlResolverCleanup, EmptyResolver) {
  // Edge case: resolver with no anchors registered
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_alias_expansion = 100;
  
  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);
  
  // Free immediately without registering anything
  gtext_yaml_resolver_free(r);
  // Run under valgrind - should show zero leaks
}

TEST(YamlResolverCleanup, ManyAnchors) {
  // Stress test: many anchors
  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.max_alias_expansion = 100000;
  
  ResolverState *r = gtext_yaml_resolver_new(&opts);
  ASSERT_NE(r, nullptr);
  
  // Register many anchors with varying sizes
  char name[32];
  for (int i = 0; i < 100; ++i) {
    snprintf(name, sizeof(name), "anchor_%d", i);
    EXPECT_EQ(gtext_yaml_resolver_register_anchor(r, name, i + 1), 1);
  }
  
  // Register many anchors with references
  for (int i = 0; i < 50; ++i) {
    snprintf(name, sizeof(name), "ref_anchor_%d", i);
    const char *refs[] = {"anchor_0", "anchor_1"};
    EXPECT_EQ(gtext_yaml_resolver_register_anchor_with_refs(r, name, 1, refs, 2), 1);
  }
  
  // Free should clean up all memory
  gtext_yaml_resolver_free(r);
  // Run under valgrind - should show zero leaks
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
