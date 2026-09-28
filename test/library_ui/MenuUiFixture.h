#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "LibraryUiStrings.h"

struct I18nFake {
  const char* get(StrId id) const { return tr(id); }
};
inline I18nFake I18N;

struct GfxRenderer {
  int width = 480, height = 800;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
};
struct MappedInputManager {};
struct Rect {
  int x, y, width, height;
};
struct MenuResult {
  int action = 0;
};
struct ActivityResult {
  bool isCancelled = false;
  int action = -1;
  ActivityResult() = default;
  ActivityResult(MenuResult result) : action(result.action) {}
};
namespace freeink::ui {
struct Insets {
  int16_t top, right, bottom, left;
};
struct ListItem {
  const char* label = nullptr;
  int16_t actionValue = 0;
};
inline constexpr int InputTouch = 1;
struct ListProps {
  const ListItem* items = nullptr;
  int count = 0, action = 0, inputMask = 0;
};
}  // namespace freeink::ui
struct UiScreen {
  freeink::ui::Insets margins{};
  freeink::ui::ListProps props;
  void setContentMarginFromScreen(freeink::ui::Insets insets) { margins = insets; }
  void list(freeink::ui::ListProps value) { props = value; }
};
struct UITheme {
  struct Metrics {
    int topPadding = 7, headerHeight = 30;
  } metrics;
  const auto& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer& r, bool, bool) const { return {11, 13, r.width - 28, r.height - 32}; }
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
};
class UiListActivity {
 public:
  GfxRenderer& renderer;
  ActivityResult result;
  int finished = 0, viewportSyncs = 0;
  struct App {
    int cleared = 0;
    void clearTapFlash() { ++cleared; }
  } app;
  static constexpr int ACTION_ROW = 2;
  UiListActivity(const char*, GfxRenderer& r, MappedInputManager&) : renderer(r) {}
  void syncListViewport(UiScreen&, freeink::ui::ListProps&) { ++viewportSyncs; }
  void setResult(ActivityResult value) { result = value; }
  void finish() { ++finished; }
};
class LibraryMenuActivity : public UiListActivity {
 public:
  static constexpr int CAPACITY = 8;
  StrId labels[CAPACITY]{};
  freeink::ui::ListItem rows[CAPACITY]{};
  std::string title;
  int count;
  LibraryMenuActivity(GfxRenderer&, MappedInputManager&, const std::string&, const StrId*, int);
  void buildScreen(UiScreen&);
  void activateIndex(int);
  void onBackButton();
};
