#include "LibraryBookDetailsActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"

namespace fui = freeink::ui;

bool LibraryBookDetailsActivity::setBook(const std::string_view title, const std::string_view path) {
  const std::string_view titleLabel = tr(STR_TITLE);
  const std::string_view pathLabel = tr(STR_LIBRARY_FILE_PATH);
  if (title.size() > MAX_TEXT_BYTES || path.size() > MAX_TEXT_BYTES || title.find('\0') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    LOG_ERR("LIB", "Invalid book details");
    return false;
  }
  const size_t bytes = titleLabel.size() + title.size() + pathLabel.size() + path.size() + 5;
  if (bytes > MAX_TEXT_BYTES) {
    LOG_ERR("LIB", "Book details exceed text limit");
    return false;
  }
  // One exact-size fallible buffer, released with the child. A stack buffer
  // would exceed the C3 budget; wrapped lines borrow this text without copies.
  auto buffer = makeUniqueNoThrow<char[]>(bytes);
  if (!buffer) {
    LOG_ERR("LIB", "OOM: book details");
    return false;
  }
  char* cursor = buffer.get();
  for (const std::string_view part :
       {titleLabel, std::string_view("\n"), title, std::string_view("\n\n"), pathLabel, std::string_view("\n"), path}) {
    if (!part.empty()) memcpy(cursor, part.data(), part.size());
    cursor += part.size();
  }
  *cursor = '\0';
  text = std::move(buffer);
  currentPage = 0;
  pageCount = 1;
  return true;
}

void LibraryBookDetailsActivity::onEnter() {
  RenderLock lock(*this);
  Activity::onEnter();
  resetUi();
  app.setScreen(&LibraryBookDetailsActivity::screenTrampoline, this);
  requestUpdate();
}

void LibraryBookDetailsActivity::turnPage(const int delta) {
  const int next = std::clamp(currentPage + delta, 0, pageCount - 1);
  if (next == currentPage) return;
  currentPage = next;
  requestUpdate();
}

void LibraryBookDetailsActivity::loop() {
  RenderLock lock(*this);
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(MenuResult{0});
    finish();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    turnPage(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
  buttonNavigator.onRelease(ButtonNavigator::getNextButtons(), [this] { turnPage(1); });
  buttonNavigator.onPreviousRelease([this] { turnPage(-1); });
  buttonNavigator.onNextContinuous([this] { turnPage(1); });
  buttonNavigator.onPreviousContinuous([this] { turnPage(-1); });
}

void LibraryBookDetailsActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<LibraryBookDetailsActivity*>(user)->buildScreen(screen);
}

void LibraryBookDetailsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{0, screen.theme().headerSidePadding, 0, screen.theme().headerSidePadding});
  const auto body = screen.contentRect();
  const auto style = screen.theme().bodyText;
  const int visible = std::max<int>(1, fui::textAreaVisibleLines(body, screen.target().lineHeight(style.font)));
  const auto lines = fui::textAreaMeasure(screen.target(), body.width, text.get(), style, 0).lineCount;
  pageCount = std::max(1, (static_cast<int>(lines) + visible - 1) / visible);
  currentPage = std::min(currentPage, pageCount - 1);
  fui::TextAreaProps props;
  props.text = text.get();
  props.topLine = static_cast<uint32_t>(currentPage * visible);
  props.showCaret = false;
  props.style = style;
  screen.textArea(props);
}

void LibraryBookDetailsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  char title[96];
  // The shared header supplies Back touch handling and the status band.
  renderUi();
  snprintf(title, sizeof(title), "%s (%d/%d)", tr(STR_LIBRARY_BOOK_DETAILS), currentPage + 1, pageCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), currentPage > 0 ? tr(STR_DIR_UP) : "",
                                            currentPage + 1 < pageCount ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
