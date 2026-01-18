#include <gtest/gtest.h>

// Basic test to ensure the library can be linked
TEST(TextLibrary, BasicTest) {
    EXPECT_TRUE(true);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
