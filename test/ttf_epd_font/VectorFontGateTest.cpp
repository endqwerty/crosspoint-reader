#include <TtfEpdFont.h>
#include <gtest/gtest.h>

static_assert(CROSSPOINT_VECTOR_FONTS == 0, "No-PSRAM builds must not compile vector-font support");
TEST(VectorFontGate, NoPsramBuildExcludesVectorFontBackend) { EXPECT_EQ(0, CROSSPOINT_VECTOR_FONTS); }
