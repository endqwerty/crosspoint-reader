#include "PageTurnFixture.h"

#include <Memory.h>
#include <Serialization.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace epub_page_test {
namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point started;
using Style = EpdFontFamily::Style;
constexpr int FONT_ID = page_test::FONT_ID;

std::vector<uint8_t> serializePage(const Page& page) {
  std::vector<uint8_t> bytes;
  bytes.reserve(16384);
  HalFile output;
  output.open(bytes);
  if (!page.serialize(output)) std::abort();
  return bytes;
}
}  // namespace

PageTurnFixture::PageTurnFixture(Scene scene) {
  auto& section = files[0];
  section.assign(SectionPageReader::HEADER_SIZE, 0);
  section.reserve(32768);
  HalFile output;
  output.open(section);
  output.seek(SectionPageReader::HEADER_SIZE);
  uint32_t positions[2];
  for (int variant = 0; variant < 2; ++variant) {
    auto page = makePage(scene, variant);
    positions[variant] = static_cast<uint32_t>(section.size());
    if (!page->serialize(output)) std::abort();
    if (variant == 1) expectedBody_ = serializePage(*page);
  }
  const uint32_t lutOffset = static_cast<uint32_t>(section.size());
  for (const auto position : positions) serialization::writePod(output, position);
  const uint32_t visibleLutOffset = static_cast<uint32_t>(section.size());
  for (uint32_t variant = 0; variant < 2; ++variant) serialization::writePod(output, VISIBLE_OFFSET + variant);
  output.seek(SectionPageReader::HEADER_SIZE - sizeof(uint32_t) * 5);
  serialization::writePod(output, lutOffset);
  output.seek(SectionPageReader::HEADER_SIZE - sizeof(uint32_t));
  serialization::writePod(output, visibleLutOffset);
}

std::unique_ptr<Page> PageTurnFixture::makePage(Scene scene, int variant) {
  auto page = makeUniqueNoThrow<Page>();
  if (!page) std::abort();
  constexpr int LINE_COUNT = 18;
  page->elements.reserve(LINE_COUNT + 1);
  for (int line = 0; line < LINE_COUNT; ++line) {
    std::vector<std::string> words;
    words.reserve(6);
    words.emplace_back(variant ? "Another" : "The");
    words.emplace_back(scene == Scene::Annotated ? "Café" : "quiet");
    words.emplace_back(line % 2 ? "reader" : "office");
    words.emplace_back(scene == Scene::Annotated ? "A\u0301" : "turns");
    words.emplace_back("pages.");
    std::vector<int16_t> positions;
    std::vector<Style> styles;
    std::vector<uint8_t> focus;
    std::vector<uint16_t> suffix;
    std::vector<std::string> ruby;
    positions.reserve(words.size());
    styles.reserve(words.size());
    const bool annotated = scene == Scene::Annotated;
    if (annotated) {
      focus.assign(words.size(), 0);
      suffix.assign(words.size(), 0);
      ruby.resize(words.size());
      if (line % 4 == 0) ruby[0] = "read";
      focus[2] = 2;
      suffix[2] = gfx.renderer.getTextAdvanceX(FONT_ID, words[2].substr(0, 2).c_str(), EpdFontFamily::BOLD);
    }
    int16_t x = 0;
    for (size_t word = 0; word < words.size(); ++word) {
      uint8_t bits = static_cast<uint8_t>((line + word) % 4);
      if (annotated && word == 0) bits |= EpdFontFamily::UNDERLINE;
      if (annotated && word == 1) bits |= EpdFontFamily::STRIKETHROUGH;
      if (annotated && word == 3) bits |= line % 2 ? EpdFontFamily::SUP : EpdFontFamily::SUB;
      const auto style = static_cast<Style>(bits);
      positions.push_back(x);
      styles.push_back(style);
      x += gfx.renderer.getTextAdvanceX(FONT_ID, words[word].c_str(), style) + gfx.renderer.getSpaceWidth(FONT_ID);
    }
    BlockStyle blockStyle;
    blockStyle.alignment = CssTextAlign::Left;
    blockStyle.textAlignDefined = true;
    blockStyle.marginLeft = variant + line % 3;
    auto block = std::make_unique<TextBlock>(words, positions, styles, focus, suffix, blockStyle, std::move(ruby));
    if (!block->valid()) std::abort();
    page->elements.push_back(std::make_unique<PageLine>(std::move(block), 0, 6 + line * 40));
  }
  page->elements.push_back(std::make_unique<PageHorizontalRule>(350, 2, 0, 750));
  page->footnotes.reserve(1);
  page->addFootnote("3", "notes.xhtml#note3");
  page->links.reserve(1);
  if (!page->addLink("chapter.xhtml#target", 12, 45, 95, 32)) std::abort();
  // Exact wire-image comparisons require initialized fixture padding. The
  // separate persistence regression tests production's unused href bytes.
  auto& link = page->links.back();
  std::fill(link.href + std::strlen(link.href) + 1, link.href + sizeof(link.href), '\0');
  page->visibleTextOffset = VISIBLE_OFFSET + variant;
  return page;
}

std::unique_ptr<Page> PageTurnFixture::loadPage(int pageIndex) {
  auto page = SectionPageReader::load("section", pageIndex);
  if (page) ++deserializations_;
  return page;
}

Work PageTurnFixture::startWork() {
  Work baseline;
  baseline.deserializations = deserializations_;
  baseline.renderPasses = renderPasses_;
  baseline.bwRefreshes = gfx.display.refreshCount;
  baseline.grayRefreshes = gfx.display.grayRefreshCount;
  io = {};
  allocations = {};
  captureAllocations = true;
  started = Clock::now();
  return baseline;
}

Work PageTurnFixture::finishWork(Work baseline) {
  const auto ended = Clock::now();
  captureAllocations = false;
  return {io,
          allocations,
          deserializations_ - baseline.deserializations,
          renderPasses_ - baseline.renderPasses,
          gfx.display.refreshCount - baseline.bwRefreshes,
          gfx.display.grayRefreshCount - baseline.grayRefreshes,
          std::chrono::duration<double, std::micro>(ended - started).count()};
}

void PageTurnFixture::prewarm(Page& page) {
  auto scope = gfx.cache.createPrewarmScope();
  ++renderPasses_;
  page.render(gfx.renderer, FONT_ID, 12, 12);
  scope.endScanAndPrewarm();
}

void PageTurnFixture::render(Page& page) {
  auto scope = gfx.cache.createPrewarmScope();
  ++renderPasses_;
  page.render(gfx.renderer, FONT_ID, 12, 12);
  scope.endScanAndPrewarm();
  gfx.renderer.clearScreen();
  gfx.renderer.setRenderMode(GfxRenderer::BW);
  ++renderPasses_;
  page.render(gfx.renderer, FONT_ID, 12, 12);
  gfx.bwBeforeComposition = gfx.display.frame;
  gfx.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    auto& output = mode == GfxRenderer::GRAYSCALE_LSB ? gfx.lsb : gfx.msb;
    gfx.renderer.setRenderMode(mode);
    gfx.renderer.beginStripTarget(output.data(), 0, HalDisplay::DISPLAY_HEIGHT);
    gfx.renderer.clearScreen(0);
    ++renderPasses_;
    page.render(gfx.renderer, FONT_ID, 12, 12);
    gfx.renderer.endStripTarget();
  }
  gfx.renderer.setRenderMode(GfxRenderer::BW);
  gfx.renderer.displayGrayBuffer();
}

TurnResult PageTurnFixture::run(bool retainPage, GfxRenderer::Orientation orientation) {
  gfx.renderer.setOrientation(orientation);
  gfx.renderer.setRenderMode(GfxRenderer::BW);
  gfx.cache.clearCache();
  TurnResult result;
  PrefetchedPageCache prefetched;
  auto work = startWork();
  auto idlePage = loadPage(1);
  if (!idlePage) std::abort();
  prewarm(*idlePage);
  const auto* idleAddress = idlePage.get();
  if (retainPage) {
    result.retainedBudgetBytes = idlePage->cacheBudgetBytes();
    if (!prefetched.retain(1, std::move(idlePage), 200 * 1024, 100 * 1024)) std::abort();
  } else {
    idlePage.reset();
  }
  result.idle = finishWork(work);

  work = startWork();
  auto page = prefetched.take(1);
  if (!page) page = loadPage(1);
  if (!page) std::abort();
  result.reusedDecodedPage = retainPage && page.get() == idleAddress;
  result.foregroundLoad = finishWork(work);
  work = startWork();
  render(*page);
  result.foregroundRender = finishWork(work);
  result.bw = gfx.display.frame;
  result.lsb = gfx.lsb;
  result.msb = gfx.msb;
  result.serializedPage = serializePage(*page);
  result.visibleTextOffset = page->visibleTextOffset;
  if (result.bw != gfx.bwBeforeComposition) std::abort();
  return result;
}
const char* sceneName(Scene scene) { return scene == Scene::Prose ? "prose" : "annotated"; }
}  // namespace epub_page_test
