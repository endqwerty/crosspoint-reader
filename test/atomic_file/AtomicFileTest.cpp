#include <gtest/gtest.h>

#include "AtomicFile.h"
#include "HalStorage.h"

namespace {
constexpr char PATH[] = "/.crosspoint/bookmarks/book.json";
const std::string NEW = std::string(PATH) + ".new";
const std::string BACKUP = std::string(PATH) + ".bak";
constexpr char BEFORE[] = "{\"bookmarks\":[\"old\"]}";
const std::string AFTER = "{\"bookmarks\":[\"" + std::string(4096, 'x') + "\"]}";
std::string read(const char* path, bool* exists = nullptr) {
  const String result = atomic_file::read(path, exists);
  return std::string(result.c_str(), result.length());
}
class AtomicFileTest : public testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    string_test::reset();
  }
  bool save() { return atomic_file::write(PATH, AFTER.data(), AFTER.size()); }
  void expectOldAndRetry() {
    EXPECT_EQ(read(PATH), BEFORE);
    fake::failReadPath.clear();
    fake::failOpenPath.clear();
    fake::blockedRenames.clear();
    fake::corruptWritePath.clear();
    ASSERT_TRUE(save());
    EXPECT_EQ(read(PATH), AFTER);
    EXPECT_FALSE(Storage.exists(BACKUP.c_str()));
  }
};
TEST_F(AtomicFileTest, FirstSaveAndReplacementRoundTripWithoutBackup) {
  bool exists = true;
  EXPECT_TRUE(read(PATH, &exists).empty());
  EXPECT_FALSE(exists);
  ASSERT_TRUE(atomic_file::write(PATH, BEFORE, sizeof(BEFORE) - 1));
  ASSERT_EQ(read(PATH, &exists), BEFORE);
  EXPECT_TRUE(exists);
  ASSERT_TRUE(save());
  EXPECT_EQ(read(PATH), AFTER);
  EXPECT_FALSE(Storage.exists(NEW.c_str()));
  EXPECT_FALSE(Storage.exists(BACKUP.c_str()));
}
TEST_F(AtomicFileTest, PartialWritePreservesCommittedBytesAndRetry) {
  fake::add(PATH, BEFORE);
  fake::partialWritePath = NEW;
  fake::partialWriteBytes = 3;
  EXPECT_FALSE(save());
  EXPECT_TRUE(fake::failureTriggered);
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, FailedCloseDoesNotPublishStagedBytes) {
  fake::add(PATH, BEFORE);
  fake::failClosePath = NEW;
  EXPECT_FALSE(save());
  EXPECT_TRUE(fake::failureTriggered);
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, ReadbackDetectsCorruptStagedData) {
  fake::add(PATH, BEFORE);
  fake::corruptWritePath = NEW;
  EXPECT_FALSE(save());
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, StageOpenFailureDoesNotTouchCommittedFile) {
  fake::add(PATH, BEFORE);
  fake::failOpenPath = NEW;
  EXPECT_FALSE(save());
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, EveryReadbackFailurePreservesPriorDocument) {
  for (int fail = 0; fail < static_cast<int>((AFTER.size() + 127) / 128); ++fail) {
    fake::reset();
    fake::add(PATH, BEFORE);
    fake::failRead = fail;
    EXPECT_FALSE(save()) << fail;
    EXPECT_TRUE(fake::failureTriggered);
    expectOldAndRetry();
  }
}
TEST_F(AtomicFileTest, BackupRenameFailureRetainsPrimary) {
  fake::add(PATH, BEFORE);
  fake::failRename = 0;
  EXPECT_FALSE(save());
  EXPECT_TRUE(fake::failureTriggered);
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, InstallFailureRollsBackAndRemainsRetryable) {
  fake::add(PATH, BEFORE);
  fake::failRename = 1;
  EXPECT_FALSE(save());
  EXPECT_TRUE(fake::failureTriggered);
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, FailedRollbackReadsBackupAndNextSaveRecovers) {
  fake::add(PATH, BEFORE);
  fake::blockedRenames = {{NEW, PATH}, {BACKUP, PATH}};
  EXPECT_FALSE(save());
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists(BACKUP.c_str()));
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, FailedCommitCleanupReadsOldDocumentRatherThanUncommittedPrimary) {
  fake::add(PATH, BEFORE);
  fake::failRemove = 0;
  EXPECT_FALSE(save());
  EXPECT_TRUE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists(BACKUP.c_str()));
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, InterruptedInstallPrefersBackupAndDiscardsUncommittedPrimaryOnRetry) {
  for (bool installed : {false, true}) {
    fake::reset();
    fake::add(BACKUP, BEFORE);
    if (installed) fake::add(PATH, AFTER);
    fake::add(NEW, "partial");
    expectOldAndRetry();
  }
}
TEST_F(AtomicFileTest, FailedRecoveryCannotDeleteTheOnlyCommittedCopy) {
  fake::add(BACKUP, BEFORE);
  fake::add(PATH, AFTER);
  fake::failRemove = 0;
  EXPECT_FALSE(save());
  EXPECT_TRUE(Storage.exists(BACKUP.c_str()));
  expectOldAndRetry();
}
TEST_F(AtomicFileTest, UncommittedFirstSaveIsNeverRead) {
  fake::add(NEW, "partial");
  bool exists = true;
  EXPECT_TRUE(read(PATH, &exists).empty());
  EXPECT_FALSE(exists);
  fake::failRename = 0;
  EXPECT_FALSE(save());
  EXPECT_TRUE(read(PATH, &exists).empty());
  EXPECT_FALSE(exists);
  ASSERT_TRUE(save());
  EXPECT_EQ(read(PATH), AFTER);
}
}  // namespace

TEST_F(AtomicFileTest, OversizedWriteDoesNotTouchCommittedDocument) {
  fake::add(PATH, BEFORE);
  std::string oversized(atomic_file::MAX_FILE_BYTES + 1, 'x');
  EXPECT_FALSE(atomic_file::write(PATH, oversized.data(), oversized.size()));
  EXPECT_EQ(read(PATH), BEFORE);
  EXPECT_FALSE(Storage.exists(NEW.c_str()));
}
TEST_F(AtomicFileTest, PathAllocationFailurePreservesPrimaryAndBackup) {
  fake::add(PATH, AFTER);
  fake::add(BACKUP, BEFORE);
  fake::failAlloc = 0;
  EXPECT_FALSE(save());
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists(BACKUP.c_str()));
  EXPECT_EQ(read(PATH), BEFORE);
  EXPECT_TRUE(save());
  EXPECT_EQ(read(PATH), AFTER);
}
TEST_F(AtomicFileTest, ReadPathAllocationFailureIsNotMistakenForFirstBoot) {
  fake::add(PATH, BEFORE);
  fake::failAlloc = 0;
  bool exists = false;
  EXPECT_TRUE(read(PATH, &exists).empty());
  EXPECT_TRUE(exists);
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_EQ(read(PATH), BEFORE);
}
