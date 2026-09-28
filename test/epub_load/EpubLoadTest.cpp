#include <gtest/gtest.h>

#include "EpubLoadFixture.h"
#include "ScopedAllocationFailure.h"

using load_test::Fault;
using load_test::state;
using Oom = parser_test::ScopedAllocationFailure;

class EpubLoadTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(state.metadataLive, 0);
    ASSERT_EQ(state.cssLive, 0);
    ASSERT_EQ(state.openHandles, 0);
    state = {};
    CssParser::status = CssParser::CacheStatus::Complete;
    CssParser::loadResult = CssParser::CacheLoadResult::Complete;
    CssParser::parseResult = CssParser::ParseResult::Complete;
  }
  void TearDown() override {
    EXPECT_EQ(state.metadataLive, 0);
    EXPECT_EQ(state.cssLive, 0);
    EXPECT_EQ(state.openHandles, 0);
    EXPECT_EQ(state.metadataValuesLive, 0);
  }
  static void expectEmpty(const Epub& epub) {
    EXPECT_FALSE(epub.bookMetadataCache);
    EXPECT_FALSE(epub.cssParser);
    EXPECT_EQ(state.metadataLive, 0);
    EXPECT_EQ(state.cssLive, 0);
    EXPECT_EQ(state.openHandles, 0);
  }
};

TEST_F(EpubLoadTest, InitialMetadataOomStopsBeforeCssAndStorage) {
  Epub epub;
  Oom oom(Oom::Kind::Object, sizeof(BookMetadataCache));
  EXPECT_FALSE(epub.load());
  EXPECT_EQ(oom.failures(), 1u);
  expectEmpty(epub);
  EXPECT_EQ(state.cssCreated, 0);
  EXPECT_EQ(state.loads, 0);
  EXPECT_EQ(state.directoryCalls, 0);
  EXPECT_GT(state.errors, 0);
}

TEST_F(EpubLoadTest, InitialCssOomReleasesMetadataBeforeReturning) {
  Epub epub;
  Oom oom(Oom::Kind::Object, sizeof(CssParser));
  EXPECT_FALSE(epub.load());
  EXPECT_EQ(oom.failures(), 1u);
  expectEmpty(epub);
  EXPECT_EQ(state.metadataCreated, 1);
  EXPECT_EQ(state.loads, 0);
  EXPECT_EQ(state.directoryCalls, 0);
  EXPECT_GT(state.errors, 0);
}

TEST_F(EpubLoadTest, WarmCssReloadOomReleasesResourcesAndPreservesSections) {
  Epub epub;
  CssParser::status = CssParser::CacheStatus::Missing;
  Oom oom(Oom::Kind::Object, sizeof(BookMetadataCache), 2);
  EXPECT_FALSE(epub.load());
  EXPECT_EQ(oom.failures(), 1u);
  expectEmpty(epub);
  EXPECT_EQ(state.cssParses, 1);
  EXPECT_EQ(state.sectionDeletes, 0);
  EXPECT_EQ(state.metadataPeak, 1);
}

TEST_F(EpubLoadTest, ColdReloadOomKeepsPublishedCacheForRetry) {
  for (bool skipCss : {false, true}) {
    state = {};
    state.cached = false;
    Epub epub;
    {
      Oom oom(Oom::Kind::Object, sizeof(BookMetadataCache), 2);
      EXPECT_FALSE(epub.load(true, skipCss));
      EXPECT_EQ(oom.failures(), 1u);
    }
    expectEmpty(epub);
    ASSERT_TRUE(state.published);
    ASSERT_TRUE(epub.load(true, skipCss));
    EXPECT_EQ(state.directoryCalls, 1);
    EXPECT_EQ(state.metadataPeak, 1);
    EXPECT_EQ(state.cssPeak, 1);
  }
}

TEST_F(EpubLoadTest, EveryColdLoadFailureReleasesOwnersAndAllowsRetry) {
  for (auto fault : {Fault::BeginWrite, Fault::BeginOpf, Fault::ParseOpf, Fault::EndOpf, Fault::BeginToc, Fault::EndToc,
                     Fault::EndWrite, Fault::Build, Fault::Reload}) {
    state = {};
    state.cached = false;
    state.fault = fault;
    Epub epub;
    EXPECT_FALSE(epub.load()) << static_cast<int>(fault);
    expectEmpty(epub);
    state.fault = Fault::None;
    ASSERT_TRUE(epub.load());
    EXPECT_EQ(state.metadataPeak, 1);
    EXPECT_EQ(state.cssPeak, 1);
  }
}

TEST_F(EpubLoadTest, RepeatedLoadReleasesPreviousOwnersBeforeReplacement) {
  Epub epub;
  ASSERT_TRUE(epub.load());
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.metadataCreated, 2);
  EXPECT_EQ(state.cssCreated, 2);
  EXPECT_EQ(state.metadataPeak, 1);
  EXPECT_EQ(state.cssPeak, 1);
  {
    Oom oom(Oom::Kind::Object, sizeof(BookMetadataCache));
    EXPECT_FALSE(epub.load());
    EXPECT_EQ(oom.failures(), 1u);
  }
  expectEmpty(epub);
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.metadataPeak, 1);
  EXPECT_EQ(state.cssPeak, 1);
}

TEST_F(EpubLoadTest, CompleteCachedLoadSkipsSourceParsingAndReleasesCssRules) {
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.loads, 1);
  EXPECT_EQ(state.opfCalls, 0);
  EXPECT_EQ(state.cssParses, 0);
  EXPECT_EQ(state.cssClears, 1);
  EXPECT_EQ(state.cssDeletes, 0);
  EXPECT_EQ(state.sectionDeletes, 0);
  EXPECT_TRUE(epub.cssParser);
  EXPECT_TRUE(epub.bookMetadataCache);
}

TEST_F(EpubLoadTest, CssLowMemoryKeepsCacheAndBookAvailable) {
  CssParser::loadResult = CssParser::CacheLoadResult::LowMemory;
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.cssDeletes, 0);
  EXPECT_EQ(state.cssParses, 0);
  EXPECT_EQ(state.sectionDeletes, 0);
  EXPECT_EQ(state.cssClears, 1);
  EXPECT_EQ(state.loads, 1);
}

TEST_F(EpubLoadTest, CssRebuildInvalidatesSectionsOnlyForChangedRules) {
  for (auto status : {CssParser::CacheStatus::Missing, CssParser::CacheStatus::Invalid, CssParser::CacheStatus::Partial,
                      CssParser::CacheStatus::Complete}) {
    for (auto result :
         {CssParser::ParseResult::Error, CssParser::ParseResult::Partial, CssParser::ParseResult::Complete}) {
      state = {};
      CssParser::status = status;
      CssParser::loadResult = CssParser::CacheLoadResult::Invalid;
      CssParser::parseResult = result;
      Epub epub;
      ASSERT_TRUE(epub.load());
      const bool changed = result == CssParser::ParseResult::Complete ||
                           (result == CssParser::ParseResult::Partial && status != CssParser::CacheStatus::Partial);
      EXPECT_EQ(state.sectionDeletes, changed ? 1 : 0);
      EXPECT_EQ(state.cssDeletes,
                status == CssParser::CacheStatus::Invalid || status == CssParser::CacheStatus::Complete);
      EXPECT_EQ(state.loads, 2);
      EXPECT_EQ(state.cssParses, 1);
      const auto beforeCss = std::find(state.events.begin(), state.events.end(), "parseCss");
      ASSERT_NE(beforeCss, state.events.begin());
      EXPECT_EQ(*(beforeCss - 1), "metadata-");
      EXPECT_EQ(state.metadataPeak, 1);
    }
  }
}

TEST_F(EpubLoadTest, CssDisabledStillCreatesInlineParserForCachedAndFreshBooks) {
  for (bool cached : {true, false}) {
    state = {};
    state.cached = cached;
    Epub epub;
    ASSERT_TRUE(epub.load(true, true));
    EXPECT_TRUE(epub.cssParser);
    EXPECT_EQ(state.cssCreated, 1);
    EXPECT_EQ(state.cssParses, 0);
    EXPECT_EQ(state.cssDeletes, 0);
    EXPECT_EQ(state.sectionDeletes, 0);
    EXPECT_EQ(state.metadataPeak, 1);
  }
}

TEST_F(EpubLoadTest, NavFallbackAndMissingTocStillAllowReading) {
  for (int mode = 0; mode < 4; ++mode) {
    state = {};
    state.cached = false;
    state.navOk = mode == 0;
    state.ncxOk = mode < 2;
    Epub epub;
    if (mode == 3) {
      epub.tocNavItem.clear();
      epub.tocNcxItem.clear();
    }
    ASSERT_TRUE(epub.load());
    EXPECT_EQ(state.navCalls, mode == 3 ? 0 : 1);
    EXPECT_EQ(state.ncxCalls, mode == 0 || mode == 3 ? 0 : 1);
    EXPECT_TRUE(state.published);
    EXPECT_EQ(state.loads, 2);
    EXPECT_EQ(state.cleanupCalls, 1);
  }
}

TEST_F(EpubLoadTest, WarmOpfFailureReloadsReadableCacheWithoutChangingSections) {
  state.fault = Fault::ParseOpf;
  CssParser::status = CssParser::CacheStatus::Missing;
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.loads, 2);
  EXPECT_EQ(state.cssParses, 0);
  EXPECT_EQ(state.sectionDeletes, 0);
  EXPECT_EQ(state.metadataPeak, 1);
}

TEST_F(EpubLoadTest, WarmReloadReadFailureReleasesOwners) {
  state.fault = Fault::Reload;
  CssParser::status = CssParser::CacheStatus::Missing;
  Epub epub;
  EXPECT_FALSE(epub.load());
  expectEmpty(epub);
  EXPECT_EQ(state.sectionDeletes, 0);
}

TEST_F(EpubLoadTest, MissingCacheWithoutBuildPermissionDoesNotWriteOrRetainOwners) {
  state.cached = false;
  Epub epub;
  EXPECT_FALSE(epub.load(false));
  expectEmpty(epub);
  EXPECT_FALSE(state.published);
  EXPECT_EQ(state.directoryCalls, 0);
  EXPECT_EQ(state.opfCalls, 0);
  EXPECT_EQ(state.cssParses, 0);
}

TEST_F(EpubLoadTest, TempCleanupFailureDoesNotRejectPublishedBook) {
  state.cached = false;
  state.cleanupOk = false;
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_TRUE(state.published);
  EXPECT_EQ(state.cleanupCalls, 1);
  EXPECT_EQ(state.loads, 2);
}

TEST_F(EpubLoadTest, TemporaryMetadataWarmCssUsesFreshOutputAndReleasesBeforeParsing) {
  CssParser::status = CssParser::CacheStatus::Missing;
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_TRUE(state.opfInputEmpty);
  EXPECT_EQ(state.metadataCopies, 0);
  EXPECT_EQ(state.metadataValuesDuringCss, 0);
  EXPECT_EQ(state.metadataValuesDuringReload, 1);
  EXPECT_EQ(epub.bookMetadataCache->coreMetadata.title, "Cached title");
}

TEST_F(EpubLoadTest, TemporaryMetadataColdBuildReleasesOutputBeforeCssAndReload) {
  for (bool skipCss : {false, true}) {
    state = {};
    state.cached = false;
    Epub epub;
    ASSERT_TRUE(epub.load(true, skipCss));
    EXPECT_TRUE(state.builtMetadataCorrect);
    EXPECT_TRUE(state.opfInputEmpty);
    EXPECT_EQ(state.metadataCopies, 0);
    EXPECT_EQ(state.metadataValuesDuringReload, 1);
    EXPECT_EQ(state.metadataValuesDuringCss, skipCss ? -1 : 0);
    EXPECT_EQ(state.metadataValuesLive, 1);
  }
}

TEST_F(EpubLoadTest, TemporaryMetadataFailedWarmOpfIsReleasedBeforeReload) {
  state.fault = Fault::ParseOpf;
  CssParser::status = CssParser::CacheStatus::Missing;
  Epub epub;
  ASSERT_TRUE(epub.load());
  EXPECT_EQ(state.metadataCopies, 0);
  EXPECT_EQ(state.metadataValuesDuringReload, 1);
  EXPECT_EQ(state.metadataValuesLive, 1);
  EXPECT_EQ(state.cssParses, 0);
  EXPECT_EQ(epub.bookMetadataCache->coreMetadata.title, "Cached title");
}

TEST_F(EpubLoadTest, TemporaryMetadataCssFailureDoesNotRetainSourceFields) {
  for (bool cached : {false, true}) {
    state = {};
    state.cached = cached;
    CssParser::status = CssParser::CacheStatus::Missing;
    CssParser::parseResult = CssParser::ParseResult::Error;
    Epub epub;
    ASSERT_TRUE(epub.load());
    EXPECT_EQ(state.metadataValuesDuringCss, 0);
    EXPECT_EQ(state.metadataValuesDuringReload, 1);
    EXPECT_EQ(state.metadataValuesLive, 1);
    EXPECT_EQ(state.sectionDeletes, 0);
  }
}
