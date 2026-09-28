#pragma once

#include <Epub/Page.h>
#include <Epub/PrefetchedPageCache.h>
#include <Epub/SectionPageReader.h>
#include <RealFontPage.h>

#include "HostAllocations.h"

namespace epub_page_test {

enum class Scene { Prose, Annotated };
struct Work {
  IoCounts io;
  AllocationCounts allocations;
  uint32_t deserializations = 0;
  uint32_t renderPasses = 0;
  uint32_t bwRefreshes = 0;
  uint32_t grayRefreshes = 0;
  double hostUs = 0;
};
struct TurnResult {
  Work idle;
  Work foregroundLoad;
  Work foregroundRender;
  page_test::Plane bw;
  page_test::Plane lsb;
  page_test::Plane msb;
  std::vector<uint8_t> serializedPage;
  uint32_t visibleTextOffset = 0;
  bool reusedDecodedPage = false;
  size_t retainedBudgetBytes = 0;
};

class PageTurnFixture {
 public:
  page_test::RealFontPage gfx;
  explicit PageTurnFixture(Scene scene);
  std::unique_ptr<Page> loadPage(int pageIndex);
  TurnResult run(bool retainPage, GfxRenderer::Orientation orientation);
  const std::vector<uint8_t>& expectedBody() const { return expectedBody_; }
  static constexpr uint32_t VISIBLE_OFFSET = 4093;

 private:
  std::vector<uint8_t> expectedBody_;
  uint32_t deserializations_ = 0;
  uint32_t renderPasses_ = 0;
  std::unique_ptr<Page> makePage(Scene scene, int variant);
  void prewarm(Page& page);
  void render(Page& page);
  Work startWork();
  Work finishWork(Work start);
};

const char* sceneName(Scene scene);
}  // namespace epub_page_test
