#include <gtest/gtest.h>

#include "HalStorage.h"
#include "LibraryBookState.h"
#include "LibrarySession.h"

using namespace library;

namespace {
constexpr uint64_t KEY = 0x0123456789abcdefULL;
constexpr char MAIN[] = "/.crosspoint/library-state/0123456789abcdef.bin";
constexpr char TEMP[] = "/.crosspoint/library-state/0123456789abcdef.new";
constexpr char BACKUP[] = "/.crosspoint/library-state/0123456789abcdef.bak";
constexpr BookState FAVORITE{true, ReadingState::Reading};
constexpr BookState FINISHED{false, ReadingState::Finished};

class LibraryBookStateTest : public testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
  void expectState(BookState expected) {
    BookState actual;
    ASSERT_TRUE(readBookState(KEY, actual));
    EXPECT_EQ(actual.favorite, expected.favorite);
    EXPECT_EQ(actual.reading, expected.reading);
  }
};
}  // namespace

TEST_F(LibraryBookStateTest, MissingStateIsUnreadAndDoesNotCreateFiles) {
  expectState({});
  EXPECT_TRUE(writeBookState(KEY, {}));
  EXPECT_TRUE(fake::files.empty());
}

TEST_F(LibraryBookStateTest, WireRecordHasFixedSizeAndLittleEndianIdentity) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  const auto& record = fake::files[MAIN]->bytes;
  EXPECT_EQ(record, (std::vector<uint8_t>{'L', 'B', 'S', '1', 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 3,
                                          static_cast<uint8_t>(3 ^ 0xa5), 0, 0}));
  expectState(FAVORITE);
}

TEST_F(LibraryBookStateTest, AllStatesPersistAndNoChangeAvoidsEveryWrite) {
  for (const auto reading : {ReadingState::Unread, ReadingState::Reading, ReadingState::Finished}) {
    for (const bool favorite : {false, true}) {
      ASSERT_TRUE(writeBookState(KEY, {favorite, reading}));
      expectState({favorite, reading});
      const auto before = fake::writesByPath;
      EXPECT_TRUE(writeBookState(KEY, {favorite, reading}));
      EXPECT_EQ(fake::writesByPath, before);
    }
  }
}

TEST_F(LibraryBookStateTest, CorruptionAndTruncationDoNotBecomeDefaultState) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  const auto original = fake::files[MAIN]->bytes;
  for (size_t byte = 0; byte < original.size(); ++byte) {
    SCOPED_TRACE(byte);
    fake::files[MAIN]->bytes = original;
    fake::files[MAIN]->bytes[byte] ^= 1;
    BookState state;
    EXPECT_FALSE(readBookState(KEY, state));
    EXPECT_FALSE(writeBookState(KEY, FINISHED));
  }
  fake::files[MAIN]->bytes = original;
  fake::files[MAIN]->bytes.pop_back();
  BookState state;
  EXPECT_FALSE(readBookState(KEY, state));
}

TEST_F(LibraryBookStateTest, DifferentKeyAndInvalidEnumAreRejected) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  const std::string other = "/.crosspoint/library-state/0123456789abcdee.bin";
  fake::files[other] = fake::files[MAIN];
  BookState state;
  EXPECT_FALSE(readBookState(KEY ^ 1, state));
  EXPECT_FALSE(writeBookState(KEY, {false, static_cast<ReadingState>(3)}));
  expectState(FAVORITE);
}

TEST_F(LibraryBookStateTest, InterruptedBackupOnlyTransactionCanRecoverAndUpdate) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  ASSERT_TRUE(Storage.rename(MAIN, BACKUP));
  expectState(FAVORITE);
  ASSERT_TRUE(writeBookState(KEY, FINISHED));
  expectState(FINISHED);
  EXPECT_FALSE(Storage.exists(BACKUP));
}

TEST_F(LibraryBookStateTest, CorruptPrimaryDoesNotReplaceValidBackupOnFailedInstall) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  ASSERT_TRUE(Storage.rename(MAIN, BACKUP));
  fake::add(MAIN, "corrupted");
  // Canonicalize backup, stage it for replacement, then fail new -> main.
  fake::failRename = 2;
  EXPECT_FALSE(writeBookState(KEY, FINISHED));
  EXPECT_TRUE(fake::failureTriggered);
  expectState(FAVORITE);
}

TEST_F(LibraryBookStateTest, EveryRenameFailurePreservesLastCommittedState) {
  for (int step = 0; step < 2; ++step) {
    fake::reset();
    ASSERT_TRUE(writeBookState(KEY, FAVORITE));
    fake::failRename = step;
    EXPECT_FALSE(writeBookState(KEY, FINISHED));
    expectState(FAVORITE);
  }
}

TEST_F(LibraryBookStateTest, WriteCloseReadbackAndOpenFailuresPreserveState) {
  for (int failure = 0; failure < 5; ++failure) {
    SCOPED_TRACE(failure);
    fake::reset();
    ASSERT_TRUE(writeBookState(KEY, FAVORITE));
    if (failure == 0) fake::failWritePath = TEMP;
    if (failure == 1) fake::failClosePath = TEMP;
    if (failure == 2) fake::failReadPath = TEMP;
    if (failure == 3) fake::failOpenPath = TEMP;
    if (failure == 4) fake::corruptWritePath = TEMP;
    EXPECT_FALSE(writeBookState(KEY, FINISHED));
    expectState(FAVORITE);
  }
}

TEST_F(LibraryBookStateTest, StaleBackupRemovalFailurePreservesBothCopies) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  fake::add(BACKUP, "old");
  fake::failRemove = 0;
  EXPECT_FALSE(writeBookState(KEY, FINISHED));
  expectState(FAVORITE);
  EXPECT_TRUE(Storage.exists(BACKUP));
}

TEST_F(LibraryBookStateTest, EveryPartialRecordWritePreservesCommittedStateAndCanRetry) {
  for (size_t cut = 1; cut < 16; ++cut) {
    SCOPED_TRACE(cut);
    fake::reset();
    ASSERT_TRUE(writeBookState(KEY, FAVORITE));
    const auto committed = fake::files[MAIN]->bytes;
    fake::partialWritePath = TEMP;
    fake::partialWriteBytes = cut;
    EXPECT_FALSE(writeBookState(KEY, FINISHED));
    ASSERT_TRUE(fake::failureTriggered);
    ASSERT_TRUE(Storage.exists(TEMP));
    EXPECT_EQ(fake::files[TEMP]->bytes.size(), cut);
    EXPECT_EQ(fake::files[MAIN]->bytes, committed);
    EXPECT_FALSE(Storage.exists(BACKUP));
    expectState(FAVORITE);
    ASSERT_TRUE(writeBookState(KEY, FINISHED));
    expectState(FINISHED);
  }
}

TEST_F(LibraryBookStateTest, FailedInstallAndRollbackKeepBackupReadableUntilRetry) {
  ASSERT_TRUE(writeBookState(KEY, FAVORITE));
  const auto committed = fake::files[MAIN]->bytes;
  fake::blockedRenames = {{TEMP, MAIN}, {BACKUP, MAIN}};
  EXPECT_FALSE(writeBookState(KEY, FINISHED));
  EXPECT_FALSE(Storage.exists(MAIN));
  ASSERT_TRUE(Storage.exists(BACKUP));
  EXPECT_EQ(fake::files[BACKUP]->bytes, committed);
  expectState(FAVORITE);
  fake::blockedRenames.clear();
  ASSERT_TRUE(writeBookState(KEY, FINISHED));
  expectState(FINISHED);
  EXPECT_FALSE(Storage.exists(BACKUP));
  EXPECT_FALSE(Storage.exists(TEMP));
}

TEST_F(LibraryBookStateTest, MarkReadingPreservesFavoritesAndFinishedStatus) {
  constexpr char PATH[] = "/Books/Title.epub";
  const uint64_t key = bookStateKey(PATH);
  ASSERT_TRUE(writeBookState(key, {true, ReadingState::Unread}));
  ASSERT_TRUE(markBookReading(PATH));
  BookState state;
  ASSERT_TRUE(readBookState(key, state));
  EXPECT_TRUE(state.favorite);
  EXPECT_EQ(state.reading, ReadingState::Reading);
  ASSERT_TRUE(writeBookState(key, {true, ReadingState::Finished}));
  const auto before = fake::writesByPath;
  EXPECT_TRUE(markBookReading(PATH));
  EXPECT_EQ(fake::writesByPath, before);
}

TEST_F(LibraryBookStateTest, ShelfFiltersAndRawPathKeysRemainIndependent) {
  EXPECT_TRUE(matchesShelfFilter(FAVORITE, ShelfFilter::All));
  EXPECT_TRUE(matchesShelfFilter(FAVORITE, ShelfFilter::Favorites));
  EXPECT_TRUE(matchesShelfFilter(FAVORITE, ShelfFilter::Reading));
  EXPECT_FALSE(matchesShelfFilter(FAVORITE, ShelfFilter::Unread));
  EXPECT_FALSE(matchesShelfFilter(FAVORITE, ShelfFilter::Finished));
  EXPECT_NE(bookStateKey("/Books/Title.epub"), bookStateKey("/Books/title.epub"));
}

TEST(LibrarySessionTest, BootMutationsAndFailedReconciliationRequireRefresh) {
  LibrarySession session;
  EXPECT_TRUE(session.needsRefresh(true, true));
  session.reconciled(true, session.refreshToken());
  EXPECT_FALSE(session.needsRefresh(true, true));
  EXPECT_TRUE(session.needsRefresh(false, true));
  EXPECT_TRUE(session.needsRefresh(true, false));
  session.invalidate();
  EXPECT_TRUE(session.needsRefresh(true, true));
  session.reconciled(false, session.refreshToken());
  EXPECT_TRUE(session.needsRefresh(true, true));
  session.reconciled(true, session.refreshToken());
  EXPECT_FALSE(session.needsRefresh(true, true));
  LibrarySession nextBoot;
  EXPECT_TRUE(nextBoot.needsRefresh(true, true));
}

TEST(LibrarySessionTest, InvalidationDuringBuildSurvivesSuccessfulCompletion) {
  LibrarySession session;
  const uint32_t started = session.refreshToken();
  session.invalidate();
  session.reconciled(true, started);
  EXPECT_TRUE(session.needsRefresh(true, true));
  const uint32_t retry = session.refreshToken();
  session.reconciled(true, retry);
  EXPECT_FALSE(session.needsRefresh(true, true));
}
