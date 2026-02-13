/**
 * @file test-yaml-arena.cpp
 * @brief Tests for YAML arena allocator
 */

#include <gtest/gtest.h>

extern "C" {
#include "../src/yaml/yaml_internal.h"
#include <stdlib.h>
#include <string.h>
}

//
// Test: Create and destroy arena
//
TEST(YamlArena, CreateDestroy) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Should have initial block
	EXPECT_NE(arena->first, nullptr);
	EXPECT_EQ(arena->first, arena->current);
	EXPECT_EQ(arena->block_size, 4096);  // 4KB initial
	
	yaml_arena_free(arena);
	// Valgrind will catch leaks
}

//
// Test: Simple allocation
//
TEST(YamlArena, SimpleAlloc) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Allocate small block
	void *p1 = yaml_arena_alloc(arena, 64, 8);
	ASSERT_NE(p1, nullptr);
	
	// Write to it (shouldn't crash)
	memset(p1, 0xAB, 64);
	
	// Allocate another
	void *p2 = yaml_arena_alloc(arena, 128, 8);
	ASSERT_NE(p2, nullptr);
	EXPECT_NE(p1, p2);  // Different pointers
	
	// Write to second
	memset(p2, 0xCD, 128);
	
	// First allocation should be unchanged
	EXPECT_EQ(((unsigned char*)p1)[0], 0xAB);
	
	yaml_arena_free(arena);
}

//
// Test: Alignment
//
TEST(YamlArena, Alignment) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Test various alignments
	void *p1 = yaml_arena_alloc(arena, 1, 1);
	EXPECT_EQ((uintptr_t)p1 % 1, 0);
	
	void *p2 = yaml_arena_alloc(arena, 1, 2);
	EXPECT_EQ((uintptr_t)p2 % 2, 0);
	
	void *p4 = yaml_arena_alloc(arena, 1, 4);
	EXPECT_EQ((uintptr_t)p4 % 4, 0);
	
	void *p8 = yaml_arena_alloc(arena, 1, 8);
	EXPECT_EQ((uintptr_t)p8 % 8, 0);
	
	void *p16 = yaml_arena_alloc(arena, 1, 16);
	EXPECT_EQ((uintptr_t)p16 % 16, 0);
	
	yaml_arena_free(arena);
}

//
// Test: Multiple blocks
//
TEST(YamlArena, MultipleBlocks) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	yaml_arena_block *first_block = arena->first;
	
	// Allocate enough to trigger new block
	// Initial block is 4KB, allocate 5KB to force new block
	void *p1 = yaml_arena_alloc(arena, 5 * 1024, 8);
	ASSERT_NE(p1, nullptr);
	
	// Should have created new block
	EXPECT_NE(arena->current, first_block);
	EXPECT_EQ(first_block->next, arena->current);
	
	// New block should be larger (8KB next)
	EXPECT_EQ(arena->block_size, 8192);
	
	yaml_arena_free(arena);
}

//
// Test: Exponential growth
//
TEST(YamlArena, ExponentialGrowth) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Start at 4KB
	EXPECT_EQ(arena->block_size, 4096);
	
	// Trigger growth to 8KB
	yaml_arena_alloc(arena, 5000, 8);
	EXPECT_EQ(arena->block_size, 8192);
	
	// Trigger growth to 16KB
	yaml_arena_alloc(arena, 9000, 8);
	EXPECT_EQ(arena->block_size, 16384);
	
	// Trigger growth to 32KB
	yaml_arena_alloc(arena, 17000, 8);
	EXPECT_EQ(arena->block_size, 32768);
	
	// Trigger growth to 64KB
	yaml_arena_alloc(arena, 33000, 8);
	EXPECT_EQ(arena->block_size, 65536);
	
	// Should cap at 64KB
	yaml_arena_alloc(arena, 65000, 8);
	EXPECT_EQ(arena->block_size, 65536);
	
	yaml_arena_free(arena);
}

//
// Test: Large allocation
//
TEST(YamlArena, LargeAlloc) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Allocate larger than max block size
	size_t large_size = 128 * 1024;  // 128KB
	void *p = yaml_arena_alloc(arena, large_size, 8);
	ASSERT_NE(p, nullptr);
	
	// Should work fine
	memset(p, 0xFF, large_size);
	EXPECT_EQ(((unsigned char*)p)[0], 0xFF);
	EXPECT_EQ(((unsigned char*)p)[large_size - 1], 0xFF);
	
	yaml_arena_free(arena);
}

//
// Test: Many small allocations
//
TEST(YamlArena, ManySmallAllocs) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Allocate 1000 small blocks
	void *ptrs[1000];
	for (int i = 0; i < 1000; i++) {
		ptrs[i] = yaml_arena_alloc(arena, 32, 8);
		ASSERT_NE(ptrs[i], nullptr);
		// Mark each allocation
		*(int*)ptrs[i] = i;
	}
	
	// Verify all allocations are distinct and intact
	for (int i = 0; i < 1000; i++) {
		EXPECT_EQ(*(int*)ptrs[i], i);
	}
	
	yaml_arena_free(arena);
}

//
// Test: Zero-size allocation
//
TEST(YamlArena, ZeroSize) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	void *p = yaml_arena_alloc(arena, 0, 8);
	EXPECT_EQ(p, nullptr);  // Should return NULL for zero size
	
	yaml_arena_free(arena);
}

//
// Test: NULL arena
//
TEST(YamlArena, NullArena) {
	void *p = yaml_arena_alloc(nullptr, 100, 8);
	EXPECT_EQ(p, nullptr);
	
	// Free NULL should be safe
	yaml_arena_free(nullptr);
}

//
// Test: Bulk free
//
TEST(YamlArena, BulkFree) {
	yaml_arena *arena = yaml_arena_new();
	ASSERT_NE(arena, nullptr);
	
	// Allocate lots of memory
	for (int i = 0; i < 100; i++) {
		yaml_arena_alloc(arena, 1024, 8);
	}
	
	// Single free cleans up everything
	yaml_arena_free(arena);
	// Valgrind will verify no leaks
}

//
// Test: Context creation
//
TEST(YamlContext, CreateDestroy) {
	yaml_context *ctx = yaml_context_new();
	ASSERT_NE(ctx, nullptr);
	
	EXPECT_NE(ctx->arena, nullptr);
	EXPECT_EQ(ctx->input_buffer, nullptr);
	EXPECT_EQ(ctx->input_buffer_len, 0);
	EXPECT_EQ(ctx->resolver, nullptr);
	EXPECT_EQ(ctx->node_count, 0);
	
	yaml_context_free(ctx);
}

//
// Test: Context allocation
//
TEST(YamlContext, Alloc) {
	yaml_context *ctx = yaml_context_new();
	ASSERT_NE(ctx, nullptr);
	
	void *p1 = yaml_context_alloc(ctx, 64, 8);
	ASSERT_NE(p1, nullptr);
	
	void *p2 = yaml_context_alloc(ctx, 128, 8);
	ASSERT_NE(p2, nullptr);
	EXPECT_NE(p1, p2);
	
	yaml_context_free(ctx);
}

//
// Test: Set input buffer
//
TEST(YamlContext, SetInputBuffer) {
	yaml_context *ctx = yaml_context_new();
	ASSERT_NE(ctx, nullptr);
	
	const char *input = "test: yaml";
	yaml_context_set_input_buffer(ctx, input, strlen(input));
	
	EXPECT_EQ(ctx->input_buffer, input);
	EXPECT_EQ(ctx->input_buffer_len, strlen(input));
	
	yaml_context_free(ctx);
	// Input buffer NOT freed (caller-owned)
}

//
// Test: NULL context safety
//
TEST(YamlContext, NullSafety) {
	yaml_context_free(nullptr);  // Should not crash
	
	void *p = yaml_context_alloc(nullptr, 100, 8);
	EXPECT_EQ(p, nullptr);
	
	yaml_context_set_input_buffer(nullptr, "test", 4);  // Should not crash
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
