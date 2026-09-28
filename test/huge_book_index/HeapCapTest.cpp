#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "HeapCap.h"

TEST(HeapCapCalibration, DirectAllocationIsChargedOnceAndReleasedAfterStop) {
  heapcap::reset(1024);
  void* pointer = ::operator new(113);
  heapcap::stop();
  EXPECT_EQ(heapcap::allocationCalls(), 1u);
  EXPECT_EQ(heapcap::live(), 113u);
  EXPECT_EQ(heapcap::peak(), 113u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  ::operator delete(pointer);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, StringReserveIsVisibleAndDestructionReleasesItsCapacity) {
  {
    std::string text;
    heapcap::reset(SIZE_MAX);
    text.reserve(4096);
    heapcap::observeByte(text.data());
    heapcap::stop();
    EXPECT_EQ(heapcap::allocationCalls(), 1u);
    EXPECT_GE(heapcap::live(), text.capacity() + 1);
    EXPECT_EQ(heapcap::peak(), heapcap::live());
  }
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, StringGrowthReleasesThePreviousAllocation) {
  {
    std::string text;
    heapcap::reset(SIZE_MAX);
    text.reserve(4096);
    heapcap::observeByte(text.data());
    const auto firstCapacity = text.capacity();
    text.reserve(firstCapacity * 2);
    heapcap::observeByte(text.data());
    heapcap::stop();
    EXPECT_EQ(heapcap::allocationCalls(), 2u);
    EXPECT_EQ(heapcap::live(), text.capacity() + 1);
    EXPECT_GT(heapcap::peak(), heapcap::live());
  }
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, NothrowFaultsDoNotConsumeBudgetAndCanRecover) {
  heapcap::reset(128, 2, 64);
  void* first = ::operator new(32, std::nothrow);
  void* injected = ::operator new(32, std::nothrow);
  void* tooLarge = ::operator new(65, std::nothrow);
  ::operator delete(first);
  void* recovered = ::operator new(64, std::nothrow);
  heapcap::stop();
  EXPECT_NE(first, nullptr);
  EXPECT_EQ(injected, nullptr);
  EXPECT_EQ(tooLarge, nullptr);
  EXPECT_NE(recovered, nullptr);
  EXPECT_EQ(heapcap::allocationCalls(), 4u);
  EXPECT_EQ(heapcap::nothrowCalls(), 4u);
  EXPECT_EQ(heapcap::injectedFailures(), 2u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  EXPECT_EQ(heapcap::live(), 64u);
  ::operator delete(recovered);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, ThrowingBudgetOverrunIsReportedWithoutStoppingTheTest) {
  heapcap::reset(32);
  void* pointer = ::operator new(64);
  heapcap::stop();
  EXPECT_EQ(heapcap::aborts(), 1u);
  EXPECT_EQ(heapcap::firstAbortSize(), 64u);
  EXPECT_EQ(heapcap::available(), 0u);
  EXPECT_EQ(heapcap::live(), 64u);
  ::operator delete(pointer);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, NestedFixtureExclusionsStillReleaseTrackedPointers) {
  heapcap::reset(1024);
  void* pointer = ::operator new(64);
  {
    heapcap::Untracked outer;
    std::string fixture(4096, 'a');
    {
      heapcap::Untracked inner;
      fixture.reserve(8192);
      ::operator delete(pointer);
    }
    fixture.reserve(16384);
  }
  void* next = ::operator new(32);
  heapcap::stop();
  EXPECT_EQ(heapcap::allocationCalls(), 2u);
  EXPECT_EQ(heapcap::live(), 32u);
  EXPECT_EQ(heapcap::peak(), 64u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  ::operator delete(next);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, StringReserveOverBudgetCannotHide) {
  {
    std::string text;
    heapcap::reset(32);
    text.reserve(4096);
    heapcap::observeByte(text.data());
    heapcap::stop();
    EXPECT_EQ(heapcap::aborts() + heapcap::uncontrolledOverruns(), 1u);
    EXPECT_GE(heapcap::firstAbortSize() + heapcap::firstUncontrolledOverrunSize(), 4097u);
    EXPECT_EQ(heapcap::available(), 0u);
    EXPECT_GE(heapcap::live(), 4097u);
  }
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, CAllocationScopeMatchesReportedCapability) {
  heapcap::reset(16);
  void* pointer = std::malloc(128);
  heapcap::stop();
  ASSERT_NE(pointer, nullptr);
  EXPECT_EQ(heapcap::allocationCalls(), heapcap::observesMalloc() ? 1u : 0u);
  EXPECT_EQ(heapcap::uncontrolledOverruns(), heapcap::observesMalloc() ? 1u : 0u);
  EXPECT_EQ(heapcap::firstUncontrolledOverrunSize(), heapcap::observesMalloc() ? 128u : 0u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  EXPECT_EQ(heapcap::live(), heapcap::observesMalloc() ? 128u : 0u);
  std::free(pointer);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, CReallocationAndFreeLeaveNoPhantomBytes) {
  heapcap::reset(SIZE_MAX);
  auto* original = static_cast<unsigned char*>(std::malloc(128));
  if (original) memset(original, 0x5A, 128);
  auto* grown = original ? static_cast<unsigned char*>(std::realloc(original, 256)) : nullptr;
  heapcap::stop();
  if (!grown) std::free(original);
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(grown[0], 0x5A);
  EXPECT_EQ(grown[127], 0x5A);
  EXPECT_EQ(heapcap::allocationCalls(), heapcap::observesMalloc() ? 2u : 0u);
  EXPECT_EQ(heapcap::live(), heapcap::observesMalloc() ? 256u : 0u);
  EXPECT_EQ(heapcap::uncontrolledOverruns(), 0u);
  std::free(grown);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, ThrowingBlockLimitIsIndependentOfTotalBudget) {
  heapcap::reset(1024, 0, 32);
  void* pointer = ::operator new(64);
  heapcap::stop();
  EXPECT_EQ(heapcap::aborts(), 1u);
  EXPECT_EQ(heapcap::uncontrolledOverruns(), 0u);
  EXPECT_EQ(heapcap::available(), 960u);
  ::operator delete(pointer);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, NothrowBudgetRefusalLeavesNoAllocation) {
  heapcap::reset(32);
  void* pointer = ::operator new(64, std::nothrow);
  heapcap::stop();
  EXPECT_EQ(pointer, nullptr);
  EXPECT_EQ(heapcap::allocationCalls(), 1u);
  EXPECT_EQ(heapcap::nothrowCalls(), 1u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  EXPECT_EQ(heapcap::uncontrolledOverruns(), 0u);
  EXPECT_EQ(heapcap::injectedFailures(), 0u);
  EXPECT_EQ(heapcap::live(), 0u);
  EXPECT_EQ(heapcap::peak(), 0u);
  ::operator delete(pointer);
}

TEST(HeapCapCalibration, ResetPeakRetainsOutstandingAllocation) {
  heapcap::reset(1024);
  void* first = ::operator new(64);
  void* second = ::operator new(32);
  ::operator delete(first);
  heapcap::resetPeak();
  heapcap::stop();
  EXPECT_EQ(heapcap::live(), 32u);
  EXPECT_EQ(heapcap::peak(), 32u);
  ::operator delete(second);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HeapCapCalibration, ZeroByteAllocationCanBeReleasedBeforeReset) {
  heapcap::reset(0);
  void* pointer = ::operator new(0);
  heapcap::stop();
  EXPECT_NE(pointer, nullptr);
  EXPECT_EQ(heapcap::allocationCalls(), 1u);
  EXPECT_EQ(heapcap::live(), 0u);
  EXPECT_EQ(heapcap::aborts(), 0u);
  ::operator delete(pointer);
  heapcap::reset(0);
  heapcap::stop();
  EXPECT_EQ(heapcap::allocationCalls(), 0u);
}
