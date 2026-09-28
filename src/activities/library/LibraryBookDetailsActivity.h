#pragma once

#include <memory>
#include <string_view>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Read-only, paged metadata. Exists only while explicitly opened from Library.
class LibraryBookDetailsActivity final : public Activity, protected UiAppHost {
 public:
  LibraryBookDetailsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("Book details", renderer, input), UiAppHost(renderer) {}
  bool setBook(std::string_view title, std::string_view path);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t MAX_TEXT_BYTES = 8192;
  static void screenTrampoline(UiScreen& screen, void* user);
  void buildScreen(UiScreen& screen);
  void turnPage(int delta);
  std::unique_ptr<char[]> text;
  int currentPage = 0;
  int pageCount = 1;
  ButtonNavigator buttonNavigator;
};
