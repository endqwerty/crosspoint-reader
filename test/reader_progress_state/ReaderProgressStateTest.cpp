#include <gtest/gtest.h>

#include "src/activities/reader/ReaderProgressState.h"

TEST(ReaderProgressStateTest, FirstPageNeedsSavingAndResetStartsANewSession) {
  ReaderProgressState state;
  const ReaderProgressState::Position first{0, 0, 24, 0};
  EXPECT_TRUE(state.needsSave(first));
  state.recordSaveResult(first, true);
  EXPECT_FALSE(state.needsSave(first));
  state.reset();
  EXPECT_TRUE(state.needsSave(first));
}

TEST(ReaderProgressStateTest, PartialSectionRedrawsCompareTheSavedEstimate) {
  ReaderProgressState state;
  constexpr int BUILT_PAGE_COUNT = 8;
  const ReaderProgressState::Position partial{3, 6, 25, 1800};
  ASSERT_NE(BUILT_PAGE_COUNT, partial.pageCount);
  EXPECT_TRUE(state.needsSave(partial));
  state.recordSaveResult(partial, true);

  // More cached pages or redraws leave the persisted estimate and position unchanged.
  for (int builtPages = BUILT_PAGE_COUNT; builtPages < 15; ++builtPages) {
    SCOPED_TRACE(builtPages);
    EXPECT_FALSE(state.needsSave(partial));
  }
}

TEST(ReaderProgressStateTest, ChangedEstimateIsSavedOnceAndFinalCountCanReplaceIt) {
  ReaderProgressState state;
  ReaderProgressState::Position position{3, 6, 25, 1800};
  state.recordSaveResult(position, true);
  position.pageCount = 27;
  EXPECT_TRUE(state.needsSave(position));
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));

  position.pageCount = 23;
  EXPECT_TRUE(state.needsSave(position));
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));
}

TEST(ReaderProgressStateTest, FailedInitialWriteIsRetried) {
  ReaderProgressState state;
  const ReaderProgressState::Position position{2, 10, 24, 6400};
  for (int attempt = 0; attempt < 3; ++attempt) {
    EXPECT_TRUE(state.needsSave(position));
    state.recordSaveResult(position, false);
  }
  EXPECT_TRUE(state.needsSave(position));
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));
}

TEST(ReaderProgressStateTest, FailedChangedWriteKeepsTheLastSuccessfulPosition) {
  ReaderProgressState state;
  const ReaderProgressState::Position saved{2, 10, 24, 6400};
  ReaderProgressState::Position pending = saved;
  ++pending.pageCount;
  state.recordSaveResult(saved, true);
  state.recordSaveResult(pending, false);
  EXPECT_FALSE(state.needsSave(saved));
  EXPECT_TRUE(state.needsSave(pending));
  state.recordSaveResult(pending, true);
  EXPECT_FALSE(state.needsSave(pending));
  EXPECT_TRUE(state.needsSave(saved));
}

TEST(ReaderProgressStateTest, PageAndSpineChangesEachNeedSaving) {
  ReaderProgressState state;
  ReaderProgressState::Position position{2, 10, 24, 6400};
  state.recordSaveResult(position, true);
  ++position.pageNumber;
  EXPECT_TRUE(state.needsSave(position));
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));
  ++position.spineIndex;
  EXPECT_TRUE(state.needsSave(position));
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));
}

TEST(ReaderProgressStateTest, VisibleOffsetChangesPersistEvenWhenPageNumbersMatch) {
  ReaderProgressState state;
  ReaderProgressState::Position position{2, 10, 24, std::nullopt};
  state.recordSaveResult(position, true);
  position.visibleTextOffset = 0;
  EXPECT_TRUE(state.needsSave(position));  // Known zero differs from the six-byte legacy payload.
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));

  position.visibleTextOffset = 6450;
  EXPECT_TRUE(state.needsSave(position));  // Reflow can change the page's visible-text start.
  state.recordSaveResult(position, true);
  EXPECT_FALSE(state.needsSave(position));

  position.visibleTextOffset.reset();
  EXPECT_TRUE(state.needsSave(position));
}
