#pragma once

#include <GfxRenderer.h>

// Keep cadence and an explicit cleanup request until the whole page commits.
class ReaderRefreshTransaction {
 public:
  ReaderRefreshTransaction(const GfxRenderer& renderer, int& cadence, bool& forcedRefresh)
      : renderer(renderer),
        cadence(cadence),
        forcedRefresh(forcedRefresh),
        previousCadence(cadence),
        previousForcedRefresh(forcedRefresh) {
    renderer.beginDisplayWork();
  }

  ~ReaderRefreshTransaction() {
    if (!renderer.endDisplayWork()) {
      cadence = previousCadence;
      forcedRefresh = forcedRefresh || previousForcedRefresh;
    }
  }

  ReaderRefreshTransaction(const ReaderRefreshTransaction&) = delete;
  ReaderRefreshTransaction& operator=(const ReaderRefreshTransaction&) = delete;

 private:
  const GfxRenderer& renderer;
  int& cadence;
  bool& forcedRefresh;
  const int previousCadence;
  const bool previousForcedRefresh;
};
