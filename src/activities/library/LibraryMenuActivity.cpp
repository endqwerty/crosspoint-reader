#include "LibraryMenuActivity.h"

#include <algorithm>

#include "components/UITheme.h"

LibraryMenuActivity::LibraryMenuActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& title,
                                         const StrId* labels, const int count)
    : UiListActivity("Library options", renderer, input), title(title), count(std::clamp(count, 0, CAPACITY)) {
  std::copy_n(labels, this->count, this->labels);
}
void LibraryMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(freeink::ui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  for (int i = 0; i < count; ++i) {
    rows[i].label = I18N.get(labels[i]);
    rows[i].actionValue = i;
  }
  freeink::ui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
void LibraryMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= count) return;
  app.clearTapFlash();
  setResult(MenuResult{index});
  finish();
}

void LibraryMenuActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
