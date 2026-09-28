// Keep the whole production bus in one translation unit. Resetting its one
// file-static semaphore models a fresh boot for each independent host fixture.
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp"

namespace bus_test {
void resetBusSemaphore() { freeink::s_epdRefreshDone = nullptr; }
}  // namespace bus_test
