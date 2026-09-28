#include "EpubSearchActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

EpubSearchActivity::EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub)
    : UiListActivity("EpubSearch", renderer, mappedInput), epub(epub) {}

void EpubSearchActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  if (!epub) {
    failed = true;
    waitingForQuery = false;
    return;
  }
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FIND_IN_BOOK), "",
                                                           epub_search::MAX_QUERY_BYTES, InputType::Text);
  if (!keyboard) {
    LOG_ERR("SRCH", "OOM: search keyboard");
    failed = true;
    waitingForQuery = false;
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    waitingForQuery = false;
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) {
      cancelSearch();
      return;
    }
    query = std::get<KeyboardResult>(result.data).text;
    if (!epub_search::ChapterSearch::validQuery(query)) {
      invalidQuery = true;
      return;
    }
    results = makeUniqueNoThrow<epub_search::Results>();
    if (!results) {
      LOG_ERR("SRCH", "OOM: results");
      failed = true;
      return;
    }
    spineCount = epub->getSpineItemsCount();
    searching = spineCount > 0;
    showSearchProgress = searching;
    if (!searching) failed = true;
  });
}

void EpubSearchActivity::onExit() {
  // No worker survives this screen. The streaming parser has already unwound.
  rows.reset();
  results.reset();
  query.clear();
  epub.reset();
  Activity::onExit();
}

bool EpubSearchActivity::cancelScan(void* context) {
  auto& self = *static_cast<EpubSearchActivity*>(context);
  self.mappedInput.update(true);
  if (self.mappedInput.isPressed(MappedInputManager::Button::Back) ||
      self.mappedInput.wasReleased(MappedInputManager::Button::Back) || self.mappedInput.wasBackGesture()) {
    self.cancelled = true;
  }
  // Home aborts now; other configured actions stay queued for the main loop.
  if (self.mappedInput.wasHomeGesture()) {
    self.cancelled = true;
    self.goHomeRequested = true;
  }
  delay(1);
  return self.cancelled;
}

void EpubSearchActivity::loop() {
  if (waitingForQuery) return;
  if (cancelled) {
    if (goHomeRequested || !mappedInput.isPressed(MappedInputManager::Button::Back)) cancelSearch();
    return;
  }
  if (searching) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
      cancelSearch();
      return;
    }
    if (showSearchProgress) {
      showSearchProgress = false;
      requestUpdateAndWait();
    }
    {
      RenderLock lock(*this);
      scanChapter();
    }
    if (cancelled && (goHomeRequested || !mappedInput.isPressed(MappedInputManager::Button::Back)))
      cancelSearch();
    else if (!searching)
      requestUpdate();
    return;
  }
  UiListActivity::loop();
}

void EpubSearchActivity::scanChapter() {
  if (nextSpine >= spineCount || nextSpine >= epub_search::MAX_SPINE_ITEMS ||
      searchedBytes >= epub_search::MAX_BOOK_BYTES || results->count >= epub_search::MAX_RESULTS) {
    partial = partial || nextSpine < spineCount;
    finishSearch();
    return;
  }
  const auto item = epub->getSpineItem(nextSpine);
  if (item.href.empty()) {
    partial = true;
    nextSpine++;
    return;
  }
  auto parser = makeUniqueNoThrow<epub_search::ChapterSearch>(
      *results, nextSpine, cancelScan, this, epub_search::MAX_XML_BYTES, epub_search::MAX_BOOK_BYTES - searchedBytes);
  if (!parser || !parser->begin(query)) {
    LOG_ERR("SRCH", "Cannot allocate/start search parser");
    failed = true;
    finishSearch();
    return;
  }
  bool read;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    read = epub->readItemContentsToStream(item.href, *parser, epub_search::CHUNK_BYTES, true);
  }
  const auto status = parser->finish(read);
  searchedBytes += parser->bytesRead();
  parser.reset();
  nextSpine++;
  if (status == epub_search::Status::Cancelled) {
    cancelled = true;
    return;
  }
  if (status != epub_search::Status::Complete) partial = true;
  if (status == epub_search::Status::OutOfMemory) failed = true;
  if (failed || results->count >= epub_search::MAX_RESULTS || nextSpine >= spineCount) finishSearch();
}

void EpubSearchActivity::finishSearch() {
  searching = false;
  if (!results || results->count == 0) return;
  rows = makeUniqueNoThrow<Rows>();
  if (!rows) {
    LOG_ERR("SRCH", "OOM: search rows");
    failed = true;
    results.reset();
    return;
  }
  for (uint8_t i = 0; i < results->count; i++) {
    const auto& result = results->items[i];
    snprintf(rows->sections[i], sizeof(rows->sections[i]), "%s%d", tr(STR_SECTION_PREFIX), result.spineIndex + 1);
    rows->items[i].label = result.snippet;
    rows->items[i].subtitle = rows->sections[i];
    rows->items[i].actionValue = i;
  }
}

void EpubSearchActivity::cancelSearch() {
  if (goHomeRequested) {
    onGoHome();
    return;
  }
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubSearchActivity::onBackButton() { cancelSearch(); }

int EpubSearchActivity::listCount() const { return rows && results ? results->count : 0; }

void EpubSearchActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  ProgressChangeResult result;
  result.spineIndex = results->items[index].spineIndex;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = results->items[index].visibleTextOffset;
  setResult(std::move(result));
  finish();
}

const char* EpubSearchActivity::headerTitle() const {
  return listCount() > 0 && (partial || failed) ? tr(STR_BOOK_SEARCH_PARTIAL_TITLE) : tr(STR_FIND_IN_BOOK);
}

void EpubSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (waitingForQuery || searching) {
    screen.centeredText(tr(STR_SEARCHING_BOOK), screen.theme().bodyText);
    return;
  }
  if (invalidQuery) {
    screen.centeredText(tr(STR_BOOK_SEARCH_INVALID), screen.theme().bodyText);
    return;
  }
  if (!listCount()) {
    const char* message = failed    ? tr(STR_BOOK_SEARCH_FAILED)
                          : partial ? tr(STR_BOOK_SEARCH_PARTIAL)
                                    : tr(STR_BOOK_SEARCH_EMPTY);
    screen.centeredText(message, screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rows->items;
  props.count = results->count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubSearchActivity::drawFooter() {
  const auto labels = listCount()
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                          : mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
